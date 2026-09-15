#include "ayu/features/aggregate/aggregate_controller.h"

#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_document.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_media_types.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include <algorithm>
#include <tuple>

namespace Ayu::Aggregate {
namespace {

constexpr auto kConcurrentRequests = 4;
constexpr auto kHistoryPage = 50;
constexpr auto kBatchRequests = 16;

} // namespace

MessageKey Key(FullMsgId id) {
	return { id.peer.value, id.msg.bare };
}

FullMsgId FullId(MessageKey key) {
	return { PeerId(key.peer), MsgId(key.message) };
}

SourceType Type(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->isBot() ? SourceType::Bot : SourceType::Private;
	} else if (const auto channel = peer->asChannel()) {
		return channel->isBroadcast() ? SourceType::Channel : SourceType::Group;
	}
	return SourceType::Group;
}

Record Describe(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const auto sublist = item->savedSublist();
	auto result = Record{
		.id = Key(item->fullId()),
		.conversation = {
			.peer = peer->id.value,
			.topic = peer->isForum() ? item->topicRootId().bare : 0,
			.sublist = (sublist && sublist->parentChat())
				? sublist->sublistPeer()->id.value : 0,
		},
		.date = item->date(),
		.album = item->groupId().raw(),
		.source = Type(peer),
	};
	const auto media = item->media();
	if (!media || media->webpage()) {
		if ((media && media->webpage()) || !item->originalText().text.isEmpty()) {
			result.categories |= Bit(Category::Text);
		}
	} else if (media->photo()) {
		result.categories |= Bit(Category::Photo);
	} else if (const auto document = media->document()) {
		const auto category = document->sticker() ? Category::Sticker
			: document->isVideoMessage() ? Category::RoundVideo
			: document->isAnimation() ? Category::Gif
			: document->isVideoFile() ? Category::Video
			: document->isVoiceMessage() ? Category::Voice
			: document->isAudioFile() ? Category::Music
			: Category::File;
		result.categories |= Bit(category);
	} else if (media->poll()) {
		result.categories |= Bit(Category::Poll);
	} else if (media->location()) {
		result.categories |= Bit(Category::Location);
	}
	if (media && media->webpage()) {
		result.categories |= Bit(Category::Link);
	}
	for (const auto &entity : item->originalText().entities) {
		if (entity.type() == EntityType::Url || entity.type() == EntityType::CustomUrl) {
			result.categories |= Bit(Category::Link);
		}
	}
	return result;
}

Controller::Controller(not_null<Main::Session*> session, FilterId folder)
: _session(session)
, _folder(folder)
, _api(&session->mtp())
, _settings(ReadFolder(session, folder))
, _parameters(ReadGlobal().parameters) {
	session->changes().peerUpdates(Data::PeerUpdate::Flag::Notifications)
	| rpl::on_next([=](const Data::PeerUpdate &update) {
		if (!_stopped && _settings.unmutedOnly && _peers.contains(update.peer->id)) {
			rebuild();
			loadMore();
		}
	}, _lifetime);
	session->data().histories().confirmedReplies() | rpl::on_next([=](const Data::Histories::ConfirmedReply &reply) {
		if (!_stopped && _records.contains(Key(reply.target))) {
			appendReply(reply.message, reply.target);
		}
	}, _lifetime);
	session->changes().messageUpdates(
		Data::MessageUpdate::Flag::NewAdded
			| Data::MessageUpdate::Flag::Edited
			| Data::MessageUpdate::Flag::Destroyed
	) | rpl::on_next([=](const Data::MessageUpdate &update) {
		if (_stopped || !_peers.contains(update.item->history()->peer->id)) {
			return;
		}
		const auto key = Key(update.item->fullId());
		if (update.flags & Data::MessageUpdate::Flag::Destroyed) {
			if (_records.contains(key) && _deleted.insert(key).second) {
				rebuild();
			}
		} else if (update.flags & Data::MessageUpdate::Flag::Edited) {
			if (const auto i = _records.find(key); i != _records.end()) {
				const auto batch = i->second.batch;
				i->second = Describe(update.item);
				i->second.batch = batch;
				rebuild();
			}
		} else if (update.item->isRegular() && update.item->id > 0
			&& active(_peers.at(update.item->history()->peer->id))
			&& IsUnseenSnapshotUpdate(
				update.item->date(),
				_snapshot,
				_records.contains(key))) {
			if (_newMessageIds.insert(key).second) {
				_hasNewMessages = true;
				_newMessages.fire({});
			}
		}
	}, _lifetime);
	SettingsChanged() | rpl::on_next([=] {
		if (!ReadGlobal().enabled) {
			stop();
			return;
		}
		if (_fixedScope) {
			return;
		}
		_parameters = ReadGlobal().parameters;
		rebuild();
	}, _lifetime);
	session->data().chatsFilters().changed() | rpl::on_next([=] {
		if (_stopped || _fixedScope) {
			return;
		}
		stop();
		if (!FolderExists(_session, _folder) || !ReadGlobal().enabled) {
			_changed.fire({});
			return;
		}
		_stopped = false;
		_directoryReady = false;
		_directoryFolder = 0;
		_directoryDate = 0;
		_directoryMessage = 0;
		_directoryPeer = 0;
		_budget = kBatchRequests;
		rebuild();
		requestDirectory();
	}, _lifetime);
}

Controller::~Controller() {
	stop();
}

void Controller::stop() {
	_stopped = true;
	++_generation;
	_api.request(base::take(_directoryRequest)).cancel();
	for (auto &[peer, source] : _peers) {
		_api.request(base::take(source.request)).cancel();
	}
	_budget = 0;
}

std::shared_ptr<Controller> ControllerFor(
		not_null<Main::Session*> session,
		FilterId folder) {
	static auto caches = std::map<Main::Session*, std::map<FilterId, std::shared_ptr<Controller>>>();
	const auto [account, added] = caches.try_emplace(session.get());
	if (added) {
		session->lifetime().add([=] { caches.erase(session.get()); });
	}
	auto &cache = account->second;
	if (const auto i = cache.find(folder); i != cache.end()) {
		return i->second;
	}
	while (cache.size() >= 4) {
		const auto i = ranges::find_if(cache, [](const auto &entry) {
			return entry.second.use_count() == 1;
		});
		if (i == cache.end()) {
			break;
		}
		cache.erase(i);
	}
	return cache.emplace(folder, std::make_shared<Controller>(session, folder)).first->second;
}

void Controller::restoreSnapshot(const Controller &snapshot) {
	stop();
	_settings = snapshot._settings;
	_parameters = snapshot._parameters;
	_snapshot = snapshot._snapshot;
	_mergeBatch = snapshot._mergeBatch;
	_peers = snapshot._peers;
	for (auto &[peer, source] : _peers) {
		source.request = 0;
	}
	_sources = snapshot._sources;
	_records = snapshot._records;
	_deleted = snapshot._deleted;
	_replies = snapshot._replies;
	_directoryReady = snapshot._directoryReady;
	_directoryFolder = snapshot._directoryFolder;
	_directoryDate = snapshot._directoryDate;
	_directoryMessage = snapshot._directoryMessage;
	_directoryPeer = snapshot._directoryPeer;
	_stopped = !ReadGlobal().enabled;
	rebuild();
}

std::shared_ptr<Controller> Controller::fork(bool unfiltered) const {
	auto result = std::make_shared<Controller>(_session, _folder);
	result->_settings = _settings;
	if (unfiltered) {
		result->_settings.categories = 0;
	}
	result->_parameters = _parameters;
	result->_snapshot = _snapshot;
	result->_mergeBatch = _mergeBatch;
	result->_fixedScope = true;
	result->_fixedFilters = _session->data().chatsFilters().list();
	result->_peers = _peers;
	for (auto &[peer, source] : result->_peers) {
		source.request = 0;
	}
	result->_sources = _sources;
	result->_records = _records;
	result->_deleted = _deleted;
	result->_replies = _replies;
	result->_directoryReady = _directoryReady;
	result->_directoryFolder = _directoryFolder;
	result->_directoryDate = _directoryDate;
	result->_directoryMessage = _directoryMessage;
	result->_directoryPeer = _directoryPeer;
	result->_stopped = _stopped;
	result->rebuild();
	return result;
}

Data::MessagesSlice Controller::mediaNavigationSlice(Mask categories, bool stream) const {
	auto ids = std::vector<FullMsgId>();
	const auto matches = [&](FullMsgId id) {
		const auto i = _records.find(Key(id));
		return i != end(_records) && !_deleted.contains(i->first)
			&& Matches(categories, i->second.categories);
	};
	if (stream) {
		for (const auto id : MediaOrder(_segments, categories)) {
			ids.push_back(FullId(id));
		}
	} else {
		auto positions = std::vector<Data::MessagePosition>();
		for (const auto &[key, record] : _records) {
			const auto id = FullId(key);
			const auto source = _peers.find(id.peer);
			if (source != end(_peers) && active(source->second) && matches(id)) {
				positions.push_back({ id, TimeId(record.date) });
			}
		}
		ranges::sort(positions, std::greater<>());
		for (const auto &position : positions) {
			ids.push_back(position.fullId);
		}
	}
	const auto count = int(ids.size());
	const auto latestAbove = !stream || _settings.newestFirst;
	return {
		.ids = std::move(ids),
		.skippedBefore = latestAbove || !hasMore() ? std::optional<int>(0) : std::nullopt,
		.skippedAfter = !latestAbove || !hasMore() ? std::optional<int>(0) : std::nullopt,
		.fullCount = hasMore() ? std::nullopt : std::make_optional(count),
	};
}

void Controller::resume() {
	if (!_snapshot || _stopped) {
		_settings = ReadFolder(_session, _folder);
		refresh(_settings.anchor.message ? _settings.snapshot : 0);
	} else {
		_changed.fire({});
	}
}

bool Controller::anchorUnavailable(FullMsgId id) const {
	if (_deleted.contains(Key(id))) {
		return true;
	}
	const auto source = _peers.find(id.peer);
	if (source == end(_peers)) {
		return _directoryReady;
	} else if (!active(source->second)) {
		return true;
	}
	const auto record = _records.find(Key(id));
	if (record != end(_records)) {
		if (Matches(_settings.categories, record->second.categories)) {
			return false;
		}
		if (record->second.album) {
			for (const auto &[key, member] : _records) {
				if (key.peer == id.peer.value
					&& member.album == record->second.album
					&& Matches(_settings.categories, member.categories)) {
					return false;
				}
			}
		}
		return true;
	}
	return source->second.exhausted
		|| (source->second.offset && source->second.offset <= id.msg);
}

int Controller::sourceCount() const {
	return ranges::count_if(_peers, [&](const auto &entry) {
		return active(entry.second);
	});
}

void Controller::appendReply(FullMsgId id, FullMsgId target) {
	const auto item = _session->data().message(id);
	if (!item || !item->isRegular() || item->id <= 0) {
		return;
	}
	_records.insert_or_assign(Key(id), Describe(item));
	_replies.insert_or_assign(Key(id), Key(target));
	_newMessageIds.erase(Key(id));
	_hasNewMessages = !_newMessageIds.empty();
	rebuild();
}

void Controller::refresh(TimeId snapshot) {
	stop();
	if (!ReadGlobal().enabled) {
		return;
	}
	_stopped = false;
	_snapshot = snapshot ? snapshot : base::unixtime::now();
	_mergeBatch = 0;
	_hasNewMessages = false;
	_settings = ReadFolder(_session, _folder);
	_parameters = ReadGlobal().parameters;
	_peers.clear();
	_sources.clear();
	_records.clear();
	_deleted.clear();
	_replies.clear();
	_newMessageIds.clear();
	_segments.clear();
	_order.clear();
	_indices.clear();
	_segmentIndices.clear();
	_directoryFolder = 0;
	_directoryDate = 0;
	_directoryMessage = 0;
	_directoryPeer = 0;
	_directoryReady = false;
	_directoryFailed = false;
	_budget = kBatchRequests;
	_session->data().histories().enumerate([&](not_null<History*> history) {
		if (history->inChatList() && contains(history)) {
			const auto item = history->lastMessage();
			_peers.try_emplace(history->peer->id, Source{
				.peer = history->peer->id,
				.frontier = item ? std::min(item->date(), _snapshot) : _snapshot,
			});
			_sources.push_back(history->peer->id);
		}
	});
	const auto archive = _session->data().folderLoaded(1);
	if (_session->data().chatsListLoaded()
		&& (!archive || _session->data().chatsListLoaded(archive))) {
		finishDirectory();
	} else {
		requestDirectory();
		pump();
	}
	_changed.fire({});
}

bool Controller::contains(not_null<History*> history) const {
	if (history->peer->migrateTo()) {
		return false;
	}
	if (!_folder) {
		return true;
	}
	for (const auto &filter : (_fixedScope ? _fixedFilters : _session->data().chatsFilters().list())) {
		if (filter.id() != _folder) {
			continue;
		}
		using Flag = Data::ChatFilter::Flag;
		if (filter.never().contains(history)) {
			return false;
		} else if (filter.always().contains(history)) {
			return true;
		} else if (const auto channel = history->peer->asChannel(); channel && channel->isCommunity()) {
			return false;
		}
		const auto type = Type(history->peer);
		const auto flag = type == SourceType::Channel ? Flag::Channels
			: type == SourceType::Group ? Flag::Groups
			: type == SourceType::Bot ? Flag::Bots
			: history->peer->asUser()->isContact() ? Flag::Contacts : Flag::NonContacts;
		return bool(filter.flags() & flag);
	}
	return false;
}

void Controller::requestDirectory() {
	if (_stopped || _directoryRequest || _directoryReady) {
		return;
	}
	const auto generation = _generation;
	_directoryFailed = false;
	_directoryRequest = _api.request(MTPmessages_GetDialogs(
		MTP_flags(MTPmessages_GetDialogs::Flag::f_folder_id
			| (_directoryPeer ? MTPmessages_GetDialogs::Flag::f_exclude_pinned : MTPmessages_GetDialogs::Flag(0))),
		MTP_int(_directoryFolder),
		MTP_int(_directoryDate),
		MTP_int(_directoryMessage),
		_directoryPeer ? _session->data().peer(_directoryPeer)->input() : MTP_inputPeerEmpty(),
		MTP_int(100),
		MTP_long(0)
	)).done([=](const MTPmessages_Dialogs &result) {
		if (generation != _generation) {
			return;
		}
		_directoryRequest = 0;
		auto ended = (result.type() == mtpc_messages_dialogs);
		auto lastPeer = PeerId();
		auto lastMessage = MsgId();
		auto lastDate = TimeId(0);
		result.match([&](const MTPDmessages_dialogsNotModified &) {
			_directoryFailed = true;
		}, [&](const auto &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			_session->data().processMessages(data.vmessages(), NewMessageType::Existing);
			ended = ended || data.vdialogs().v.empty();
			for (const auto &entry : data.vdialogs().v) {
				entry.match([&](const MTPDdialog &dialog) {
					const auto peer = peerFromMTP(dialog.vpeer());
					const auto id = MsgId(dialog.vtop_message().v);
					const auto item = _session->data().message(peer, id);
					if (id && item && !dialog.is_pinned()) {
						lastPeer = peer;
						lastMessage = id;
						lastDate = item->date();
					}
					const auto history = _session->data().history(peer);
					if (contains(history)) {
						_peers.try_emplace(peer, Source{
							.peer = peer,
							.frontier = item ? item->date() : _snapshot,
						});
						if (item && item->date() > _snapshot && active(_peers.at(peer))) {
							_newMessageIds.insert(Key(item->fullId()));
							_hasNewMessages = true;
							_newMessages.fire({});
						}
					}
				}, [](const MTPDdialogFolder &) {
				}, [](const MTPDdialogCommunity &) {
				});
			}
		});
		if (_directoryFailed) {
			_changed.fire({});
			return;
		}
		if (!ended && (!lastPeer
			|| (lastPeer == _directoryPeer && lastMessage == _directoryMessage))) {
			_directoryFailed = true;
			_changed.fire({});
			return;
		}
		if (ended) {
			if (_directoryFolder == 1) {
				finishDirectory();
				return;
			}
			_directoryFolder = 1;
			lastDate = 0;
			lastMessage = 0;
			lastPeer = 0;
		}
		_sources.clear();
		for (const auto &[peer, source] : _peers) {
			_sources.push_back(peer);
		}
		pump();
		_directoryDate = lastDate;
		_directoryMessage = lastMessage;
		_directoryPeer = lastPeer;
		requestDirectory();
	}).fail([=] {
		if (generation == _generation) {
			_directoryRequest = 0;
			_directoryFailed = true;
			_changed.fire({});
		}
	}).send();
}

void Controller::finishDirectory() {
	_directoryReady = true;
	for (const auto &filter : (_fixedScope ? _fixedFilters : _session->data().chatsFilters().list())) {
		if (filter.id() == _folder) {
			for (const auto history : filter.always()) {
				if (contains(history)) {
					const auto peer = history->peer->id;
					_peers.try_emplace(peer, Source{ .peer = peer, .frontier = _snapshot });
				}
			}
		}
	}
	_sources.clear();
	for (const auto &[peer, source] : _peers) {
		if (contains(_session->data().history(peer))) {
			_sources.push_back(peer);
			const auto item = _session->data().history(peer)->lastMessage();
			if (item && item->id > 0 && item->date() > _snapshot
				&& active(source)) {
				_newMessageIds.insert(Key(item->fullId()));
			}
		}
	}
	if (!_newMessageIds.empty() && !_hasNewMessages) {
		_hasNewMessages = true;
		_newMessages.fire({});
	}
	rebuild();
	pump();
}

bool Controller::active(const Source &source) const {
	return contains(_session->data().history(source.peer))
		&& (!_settings.unmutedOnly || !_session->data().history(source.peer)->muted())
		&& !ranges::contains(_settings.excluded, source.peer.value)
		&& Matches(_settings.sources, Bit(Type(_session->data().peer(source.peer))));
}

void Controller::pump() {
	if (_stopped) {
		return;
	}
	auto pending = 0;
	for (const auto &[peer, source] : _peers) {
		pending += (source.request != 0);
	}
	while (_budget > 0 && pending < kConcurrentRequests) {
		auto candidate = (Source*)nullptr;
		for (auto &[peer, source] : _peers) {
			if (!source.exhausted && !source.failed && !source.request && active(source)
				&& (!candidate || source.frontier > candidate->frontier)) {
				candidate = &source;
			}
		}
		if (!candidate) {
			break;
		}
		--_budget;
		++pending;
		requestHistory(*candidate);
	}
	_changed.fire({});
}

void Controller::requestHistory(Source &source) {
	const auto peer = source.peer;
	const auto generation = _generation;
	const auto oldOffset = source.offset;
	source.request = _api.request(MTPmessages_GetHistory(
		_session->data().peer(peer)->input(),
		MTP_int(oldOffset),
		MTP_int(oldOffset ? 0 : _snapshot + 1),
		MTP_int(0),
		MTP_int(kHistoryPage),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		if (generation != _generation) {
			return;
		}
		auto &source = _peers.at(peer);
		source.request = 0;
		auto count = 0;
		result.match([&](const MTPDmessages_messagesNotModified &) {
			source.failed = true;
		}, [&](const auto &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			for (const auto &message : data.vmessages().v) {
				const auto item = _session->data().addNewMessage(message, {}, NewMessageType::Existing);
				++count;
				if (!item || item->id <= 0) {
					continue;
				}
				if (!source.offset || item->id < source.offset) {
					source.offset = item->id;
				}
				source.frontier = std::min(source.frontier, item->date());
				if (item->isRegular() && item->date() <= _snapshot) {
					const auto key = Key(item->fullId());
					auto record = Describe(item);
					const auto existing = _records.find(key);
					record.batch = (existing != end(_records))
						? existing->second.batch : _mergeBatch;
					_records.insert_or_assign(key, record);
					_newMessageIds.erase(key);
				}
			}
		});
		_hasNewMessages = !_newMessageIds.empty();
		if (!source.failed) {
			source.exhausted = (count == 0);
			source.failed = count && (source.offset == oldOffset);
		}
		rebuild();
		pump();
	}).fail([=] {
		if (generation == _generation) {
			auto &source = _peers.at(peer);
			source.request = 0;
			source.failed = true;
			rebuild();
			pump();
		}
	}).send();
}

void Controller::rebuild() {
	auto records = std::vector<Record>();
	auto exhausted = std::map<std::uint64_t, bool>();
	for (const auto &[peer, source] : _peers) {
		if (!active(source)) {
			continue;
		}
		exhausted.emplace(peer.value, source.exhausted);

	}
	for (const auto &[key, record] : _records) {
		if (exhausted.contains(key.peer) && !_replies.contains(key)) {
			records.push_back(record);
		}
	}
	auto segments = MakeSegments(records, _parameters, exhausted);
	std::erase_if(segments, [&](Segment &segment) {
		const auto &source = _peers.at(PeerId(segment.conversation.peer));
		if (!segment.complete && !segment.records.empty()) {
			segment.complete = segment.records.front().date - source.frontier
				> Gap(segment.records.front().source, _parameters);
		}
		return !segment.complete;
	});
	for (auto &segment : segments) {
		std::erase_if(segment.records, [&](const Record &record) { return _deleted.contains(record.id); });
	}
	for (const auto &[id, target] : _replies) {
		if (_deleted.contains(id) || !exhausted.contains(id.peer)) {
			continue;
		}
		const auto record = _records.find(id);
		if (record == _records.end()) {
			continue;
		}
		AppendConfirmedReply(segments, record->second, target);
	}
	segments = MergeAdjacentChannels(std::move(segments));
	_segments = FilterSegments(segments, _settings.categories, _settings.sources, _settings.newestFirst);
	_order.clear();
	_indices.clear();
	_segmentIndices.clear();
	auto segmentIndex = 0;
	for (const auto &segment : _segments) {
		for (const auto &record : segment.records) {
			const auto id = FullId(record.id);
			_indices.emplace(id, int(_order.size()));
			_segmentIndices.emplace(id, segmentIndex);
			_order.push_back(id);
		}
		++segmentIndex;
	}
	_changed.fire({});
}

void Controller::loadMore() {
	if (_stopped || loading()) {
		return;
	}
	++_mergeBatch;
	for (auto &[key, record] : _records) {
		if (!_indices.contains(FullId(key))) {
			record.batch = _mergeBatch;
		}
	}
	_budget = kBatchRequests;
	if (!_directoryReady) {
		requestDirectory();
	}
	pump();
}

void Controller::retry() {
	if (!_directoryReady) {
		requestDirectory();
		return;
	}
	for (auto &[peer, source] : _peers) {
		source.failed = false;
	}
	loadMore();
}

void Controller::setFilters(Mask categories, Mask sources, bool newestFirst) {
	const auto unmutedOnly = ReadFolder(_session, _folder).unmutedOnly;
	if (categories == _settings.categories && sources == _settings.sources
		&& unmutedOnly == _settings.unmutedOnly) {
		if (newestFirst != _settings.newestFirst) {
			_settings.newestFirst = newestFirst;
			rebuild();
		}
		return;
	}
	const auto directoryPending = !_directoryReady;
	stop();
	_stopped = !ReadGlobal().enabled;
	_settings.unmutedOnly = unmutedOnly;
	_settings.categories = categories;
	_settings.sources = sources;
	_settings.newestFirst = newestFirst;
	rebuild();
	if (directoryPending) {
		_budget = kBatchRequests;
		requestDirectory();
	} else {
		loadMore();
	}
}

void Controller::exclude(PeerId peer, bool excluded) {
	const auto directoryPending = !_directoryReady;
	stop();
	_stopped = !ReadGlobal().enabled;
	std::erase(_settings.excluded, peer.value);
	if (excluded) {
		_settings.excluded.push_back(peer.value);
	}
	auto saved = ReadFolder(_session, _folder);
	saved.excluded = _settings.excluded;
	WriteFolder(_session, _folder, saved, false);
	rebuild();
	if (directoryPending) {
		_budget = kBatchRequests;
		requestDirectory();
	} else {
		loadMore();
	}
}

const std::vector<Segment> &Controller::segments() const {
	return _segments;
}

const std::vector<PeerId> &Controller::sources() const {
	return _sources;
}

Data::MessagesSlice Controller::slice(FullMsgId around, int before, int after) const {
	const auto count = int(_order.size());
	const auto index = indexOf(around);
	const auto center = (index >= 0) ? index : _settings.newestFirst ? 0 : std::max(0, count - 1);
	auto from = std::max(0, center - std::clamp(before, 16, 256));
	auto till = std::min(count, center + std::clamp(after, 16, 256) + 1);
	const auto inseparable = [&](FullMsgId a, FullMsgId b) {
		const auto segment = segmentFor(a);
		if (!segment || segment != segmentFor(b)) return false;
		if (segment->records.front().source != SourceType::Channel) return true;
		const auto first = _records.find(Key(a));
		const auto second = _records.find(Key(b));
		return first != _records.end() && second != _records.end()
			&& first->second.album && first->second.album == second->second.album;
	};
	while (from > 0 && inseparable(_order[from - 1], _order[from])) {
		--from;
	}
	while (till < count && inseparable(_order[till - 1], _order[till])) {
		++till;
	}
	return {
		.ids = std::vector<FullMsgId>(_order.begin() + from, _order.begin() + till),
		.nearestToAround = count ? _order[center] : FullMsgId(),
		.skippedBefore = _settings.newestFirst || !hasMore() ? std::optional<int>(from) : std::nullopt,
		.skippedAfter = !_settings.newestFirst || !hasMore() ? std::optional<int>(count - till) : std::nullopt,
		.fullCount = hasMore() ? std::nullopt : std::make_optional(count),
	};
}

Data::MessagesSlice Controller::mediaSlice(
		Category category,
		Data::MessagePosition around,
		int limit) const {
	auto positions = std::vector<Data::MessagePosition>();
	for (const auto &[key, record] : _records) {
		const auto source = _peers.find(FullId(key).peer);
		if (source != end(_peers)
			&& active(source->second)
			&& !_deleted.contains(key)
			&& Matches(Bit(category), record.categories)) {
			positions.push_back({ FullId(key), TimeId(record.date) });
		}
	}
	ranges::sort(positions, std::greater<>());
	const auto count = int(positions.size());
	const auto found = ranges::lower_bound(positions, around, std::greater<>());
	const auto center = std::min(int(found - begin(positions)), std::max(0, count - 1));
	limit = std::clamp(limit, 16, 256);
	const auto from = std::max(0, center - limit);
	const auto till = std::min(count, center + limit + 1);
	auto result = Data::MessagesSlice{
		.nearestToAround = count ? positions[center].fullId : FullMsgId(),
		.skippedBefore = from,
		.skippedAfter = hasMore() ? std::nullopt : std::make_optional(count - till),
		.fullCount = hasMore() ? std::nullopt : std::make_optional(count),
	};
	for (auto i = from; i != till; ++i) {
		result.ids.push_back(positions[i].fullId);
	}
	return result;
}

int Controller::indexOf(FullMsgId id) const {
	const auto i = _indices.find(id);
	return i == _indices.end() ? -1 : i->second;
}

const Segment *Controller::segmentFor(FullMsgId id) const {
	const auto i = _segmentIndices.find(id);
	return i == _segmentIndices.end() ? nullptr : &_segments[i->second];
}

bool Controller::loading() const {
	return _directoryRequest || ranges::any_of(_peers, [](const auto &entry) {
		return entry.second.request != 0;
	});
}

bool Controller::hasMore() const {
	return !_directoryReady || ranges::any_of(_peers, [&](const auto &entry) {
		return active(entry.second) && !entry.second.exhausted;
	});
}

bool Controller::failed() const {
	return _directoryFailed || ranges::any_of(_peers, [&](const auto &entry) {
		return active(entry.second) && entry.second.failed;
	});
}

TimeId Controller::snapshot() const {
	return _snapshot;
}

rpl::producer<> Controller::changed() const {
	return _changed.events();
}

rpl::producer<> Controller::newMessages() const {
	return _newMessages.events();
}

QString Controller::title() const {
	for (const auto &filter : (_fixedScope ? _fixedFilters : _session->data().chatsFilters().list())) {
		if (_folder && filter.id() == _folder) {
			return filter.titleText().text;
		}
	}
	return tr::lng_filters_all(tr::now);
}

} // namespace Ayu::Aggregate
