// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/forward/ayu_forward.h"

#include "api/api_sending.h"
#include "apiwrap.h"
#include "ayu/features/forward/ayu_forward_rich.h"
#include "ayu/features/forward/ayu_sync.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/random.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang_auto.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/text_utilities.h"

#include "styles/style_boxes.h"

namespace AyuForward {
namespace {

struct StableAction {
	PeerId peerId;
	FullReplyTo replyTo;
	Api::SendOptions options;
	PeerId sendAsId;
};

struct ForwardChunk {
	bool reconstruct = false;
	std::vector<FullMsgId> items;
};

struct PreparedGroup {
	Ui::PreparedList list;
	std::vector<FullMsgId> items;
	SendMediaType type = SendMediaType::File;
};

class Operation final
	: public std::enable_shared_from_this<Operation> {
public:
	Operation(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		const Data::ResolvedForwardDraft &draft,
		bool reconstructAll,
		CompletionCallback completion);

	void start();
	void cancel();
	void sessionLost();
	[[nodiscard]] PeerId peerId() const;
	[[nodiscard]] const std::shared_ptr<ForwardState> &state() const;

private:
	[[nodiscard]] Main::Session *session() const;
	[[nodiscard]] HistoryItem *resolve(FullMsgId itemId) const;
	[[nodiscard]] Api::SendAction takeAction(int messageCount);
	[[nodiscard]] Api::SendAction takeUnitAction(int messageCount);
	[[nodiscard]] TextWithTags fallbackText(FullMsgId itemId) const;
	[[nodiscard]] TextWithTags mediaCaption(FullMsgId itemId) const;
	[[nodiscard]] bool preflight() const;
	void buildChunks(bool reconstructAll);
	void startNextChunk();
	void startForwardChunk();
	void forwardChunkFinished(bool success);
	void startNextUnit();
	void startCurrentUnit();
	void richMessageFinished(bool success);
	void startDownload();
	void downloadFinished(bool success);
	[[nodiscard]] bool prepareCurrentUnit();
	void sendNextPreparedGroup();
	void preparedGroupFinished(bool success);
	void unitFinished();
	void finish(Result result);
	void finishDetached(Result result);

	base::weak_ptr<Main::Session> _session;
	StableAction _action;
	std::vector<FullMsgId> _items;
	Data::ForwardOptions _options = Data::ForwardOptions::PreserveInfo;
	std::vector<ForwardChunk> _chunks;
	std::vector<FullMsgId> _currentUnit;
	std::vector<PreparedGroup> _preparedGroups;
	std::unique_ptr<rpl::lifetime> _stepLifetime;
	std::shared_ptr<RichForwardRequest> _richRequest;
	std::optional<Api::SendAction> _unitAction;
	std::shared_ptr<ForwardState> _state;
	CompletionCallback _completion;
	int _chunkIndex = 0;
	int _unitOffset = 0;
	int _downloadIndex = 0;
	int _preparedIndex = 0;
	bool _cancelRequested = false;
	bool _finished = false;

};

struct SessionOperations {
	std::deque<std::shared_ptr<Operation>> queue;
};

base::flat_map<Main::Session*, SessionOperations> Operations;
base::flat_map<
	Main::Session*,
	base::flat_map<PeerId, std::weak_ptr<Operation>>> VisibleOperations;

void OperationFinished(
		not_null<Main::Session*> session,
		const std::shared_ptr<Operation> &operation) {
	const auto i = Operations.find(session);
	if (i == Operations.end()) {
		return;
	}
	auto &queue = i->second.queue;
	if (!queue.empty() && queue.front() == operation) {
		queue.pop_front();
	} else {
		const auto found = ranges::find(queue, operation);
		if (found != queue.end()) {
			queue.erase(found);
		}
	}
	const auto visibleSession = VisibleOperations.find(session);
	if (visibleSession != VisibleOperations.end()) {
		auto &visible = visibleSession->second;
		const auto entry = visible.find(operation->peerId());
		if (entry != visible.end() && entry->second.lock() == operation) {
			visible.erase(entry);
		}
		if (visible.empty()) {
			VisibleOperations.erase(visibleSession);
		}
	}
	if (queue.empty()) {
		Operations.erase(i);
	} else {
		queue.front()->start();
	}
}

void SessionLost(Main::Session *session) {
	const auto i = Operations.find(session);
	if (i == Operations.end()) {
		return;
	}
	const auto queue = i->second.queue;
	Operations.erase(i);
	VisibleOperations.remove(session);
	for (const auto &operation : queue) {
		operation->sessionLost();
	}
}

void Submit(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		const Data::ResolvedForwardDraft &draft,
		bool reconstructAll,
		CompletionCallback completion) {
	const auto operation = std::make_shared<Operation>(
		session,
		action,
		draft,
		reconstructAll,
		std::move(completion));
	const auto existing = Operations.find(session);
	if (existing == Operations.end()) {
		session->lifetime().add([session] {
			SessionLost(session);
		});
	}
	auto &queue = Operations[session].queue;
	const auto start = queue.empty();
	queue.push_back(operation);
	if (start) {
		operation->start();
	}
}

SendMediaType MediaType(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document) {
		return SendMediaType::Photo;
	} else if (document->isVoiceMessage()) {
		return SendMediaType::Audio;
	} else if (document->isVideoMessage()) {
		return SendMediaType::Round;
	} else if (document->isVideoFile() || document->isGifv()) {
		return SendMediaType::Photo;
	}
	return SendMediaType::File;
}

Operation::Operation(
	not_null<Main::Session*> session,
	const Api::SendAction &action,
	const Data::ResolvedForwardDraft &draft,
	bool reconstructAll,
	CompletionCallback completion)
: _session(base::make_weak(session.get()))
, _action({
	.peerId = action.history->peer->id,
	.replyTo = action.replyTo,
	.options = action.options,
	.sendAsId = action.options.sendAs
		? action.options.sendAs->id
		: PeerId(),
})
, _options(draft.options)
, _state(std::make_shared<ForwardState>())
, _completion(std::move(completion)) {
	_action.options.sendAs = nullptr;
	_items.reserve(draft.items.size());
	for (const auto item : draft.items) {
		_items.push_back(item->fullId());
	}
	buildChunks(reconstructAll);
	_state->totalChunks = int(_chunks.size());
	_state->totalMessages = int(_items.size());
}

Main::Session *Operation::session() const {
	return _session.get();
}

HistoryItem *Operation::resolve(FullMsgId itemId) const {
	const auto current = session();
	return current ? current->data().message(itemId) : nullptr;
}

Api::SendAction Operation::takeAction(int messageCount) {
	const auto current = session();
	Assert(current != nullptr);
	const auto history = current->data().history(_action.peerId);
	auto options = _action.options;
	const auto starsApproved = std::min(
		options.starsApproved,
		messageCount * history->peer->starsPerMessageChecked());
	_action.options.starsApproved -= starsApproved;
	options.starsApproved = starsApproved;
	options.sendAs = _action.sendAsId
		? current->data().peer(_action.sendAsId).get()
		: nullptr;
	auto result = Api::SendAction(history, options);
	result.replyTo = _action.replyTo;
	result.clearDraft = false;
	return result;
}

Api::SendAction Operation::takeUnitAction(int messageCount) {
	if (_unitAction) {
		auto result = *_unitAction;
		_unitAction.reset();
		return result;
	}
	return takeAction(messageCount);
}

TextWithTags Operation::fallbackText(FullMsgId itemId) const {
	const auto item = resolve(itemId);
	if (!item) {
		return {};
	}
	auto result = extractText(item);
	if (!result.text.isEmpty()) {
		return result;
	}
	const auto notification = item->notificationText();
	result.text = notification.text;
	result.tags = TextUtilities::ConvertEntitiesToTextTags(
		notification.entities);
	return result;
}

TextWithTags Operation::mediaCaption(FullMsgId itemId) const {
	const auto item = resolve(itemId);
	if (!item) {
		return {};
	}
	const auto media = item->media();
	if (media
		&& media->allowsEditCaption()
		&& _options == Data::ForwardOptions::NoNamesAndCaptions) {
		return {};
	}
	return fallbackText(itemId);
}

bool Operation::preflight() const {
	for (const auto &chunk : _chunks) {
		if (!chunk.reconstruct) {
			continue;
		}
		for (const auto itemId : chunk.items) {
			const auto item = resolve(itemId);
			if (!item) {
				return false;
			}
			if (item->richPage()) {
				continue;
			}
			const auto media = item->media();
			if ((!media || (!media->photo() && !media->document()))
				&& fallbackText(itemId).text.isEmpty()) {
				return false;
			}
		}
	}
	return true;
}

void Operation::buildChunks(bool reconstructAll) {
	for (auto i = 0; i != int(_items.size());) {
		const auto item = resolve(_items[i]);
		if (!item) {
			_chunks.push_back({
				.reconstruct = true,
				.items = { _items[i] },
			});
			++i;
			continue;
		}
		const auto groupId = item->groupId();
		auto end = i + 1;
		if (groupId) {
			while (end != int(_items.size())) {
				const auto next = resolve(_items[end]);
				if (!next || next->groupId() != groupId) {
					break;
				}
				++end;
			}
		}
		auto reconstruct = reconstructAll;
		for (auto index = i; index != end && !reconstruct; ++index) {
			const auto current = resolve(_items[index]);
			reconstruct = !current || isAyuForwardNeeded(current);
		}
		if (_chunks.empty()
			|| _chunks.back().reconstruct != reconstruct) {
			_chunks.push_back({ .reconstruct = reconstruct });
		}
		_chunks.back().items.insert(
			_chunks.back().items.end(),
			_items.begin() + i,
			_items.begin() + end);
		i = end;
	}
}

void Operation::start() {
	if (_finished) {
		return;
	}
	const auto current = session();
	if (!current || _items.empty() || !preflight()) {
		finish(Result::Failure);
		return;
	}
	VisibleOperations[current][_action.peerId] = shared_from_this();
	const auto history = current->data().history(_action.peerId);
	history->setForwardDraft(
		_action.replyTo.topicRootId,
		_action.replyTo.monoforumPeerId,
		{});
	_state->updateBottomBar(
		current,
		_action.peerId,
		ForwardState::State::Preparing);
	startNextChunk();
}

void Operation::cancel() {
	if (_finished) {
		return;
	}
	_cancelRequested = true;
	_state->stopRequested = true;
	if (_richRequest) {
		_richRequest->cancel();
		_richRequest.reset();
	}
	_stepLifetime.reset();
	finish(Result::Cancelled);
}

void Operation::sessionLost() {
	finishDetached(Result::Failure);
}

PeerId Operation::peerId() const {
	return _action.peerId;
}

const std::shared_ptr<ForwardState> &Operation::state() const {
	return _state;
}

void Operation::startNextChunk() {
	if (_cancelRequested) {
		finish(Result::Cancelled);
		return;
	} else if (_chunkIndex == int(_chunks.size())) {
		finish(Result::Success);
		return;
	}
	_state->currentChunk = _chunkIndex;
	_unitOffset = 0;
	if (_chunks[_chunkIndex].reconstruct) {
		startNextUnit();
	} else {
		startForwardChunk();
	}
}

void Operation::startForwardChunk() {
	const auto current = session();
	if (!current) {
		finish(Result::Failure);
		return;
	}
	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto itemId : _chunks[_chunkIndex].items) {
		const auto item = resolve(itemId);
		if (!item) {
			finish(Result::Failure);
			return;
		}
		items.push_back(item);
	}
	_state->updateBottomBar(
		current,
		_action.peerId,
		ForwardState::State::Sending);
	const auto self = shared_from_this();
	current->api().forwardMessages(
		Data::ResolvedForwardDraft(items, _options),
		takeAction(int(items.size())),
		[self](bool success) { self->forwardChunkFinished(success); });
}

void Operation::forwardChunkFinished(bool success) {
	if (!success) {
		finish(Result::Failure);
		return;
	}
	_state->sentMessages += int(_chunks[_chunkIndex].items.size());
	++_chunkIndex;
	startNextChunk();
}

void Operation::startNextUnit() {
	if (_cancelRequested) {
		finish(Result::Cancelled);
		return;
	}
	const auto &items = _chunks[_chunkIndex].items;
	if (_unitOffset == int(items.size())) {
		++_chunkIndex;
		startNextChunk();
		return;
	}
	const auto first = resolve(items[_unitOffset]);
	if (!first) {
		finish(Result::Failure);
		return;
	}
	_currentUnit.clear();
	_unitAction.reset();
	const auto groupId = first->groupId();
	do {
		const auto itemId = items[_unitOffset++];
		const auto item = resolve(itemId);
		if (!item) {
			finish(Result::Failure);
			return;
		}
		_currentUnit.push_back(itemId);
		if (!groupId || _unitOffset == int(items.size())) {
			break;
		}
		const auto next = resolve(items[_unitOffset]);
		if (!next || next->groupId() != groupId) {
			break;
		}
	} while (true);
	if (_currentUnit.size() == 1 && first->richPage()) {
		_unitAction = takeAction(1);
		const auto self = shared_from_this();
		_state->updateBottomBar(
			session(),
			_action.peerId,
			ForwardState::State::Downloading);
		_richRequest = forwardRichMessage(
			session(),
			_currentUnit.front(),
			*_unitAction,
			[self](bool success) {
				self->richMessageFinished(success);
			});
		return;
	}
	startCurrentUnit();
}

void Operation::startCurrentUnit() {
	const auto first = resolve(_currentUnit.front());
	if (!first) {
		finish(Result::Failure);
		return;
	}
	const auto media = first->media();
	if (!media || (!media->photo() && !media->document())) {
		_preparedGroups.clear();
		_preparedIndex = 0;
		auto message = Api::MessageToSend(takeUnitAction(1));
		message.textWithTags = fallbackText(_currentUnit.front());
		if (message.textWithTags.text.isEmpty()) {
			finish(Result::Failure);
			return;
		}
		const auto self = shared_from_this();
		message.completion = Api::MakeSendCompletion([self](bool success) {
			self->preparedGroupFinished(success);
		});
		_state->updateBottomBar(
			session(),
			_action.peerId,
			ForwardState::State::Sending);
		session()->api().sendMessage(std::move(message));
		return;
	}
	_downloadIndex = 0;
	startDownload();
}

void Operation::richMessageFinished(bool success) {
	_richRequest.reset();
	if (_finished) {
		return;
	} else if (_cancelRequested) {
		finish(Result::Cancelled);
		return;
	} else if (success) {
		_unitAction.reset();
		unitFinished();
		return;
	}
	startCurrentUnit();
}

void Operation::startDownload() {
	if (_downloadIndex == int(_currentUnit.size())) {
		if (!prepareCurrentUnit()) {
			finish(Result::Failure);
			return;
		}
		_preparedIndex = 0;
		sendNextPreparedGroup();
		return;
	}
	const auto current = session();
	if (!current) {
		finish(Result::Failure);
		return;
	}
	_state->updateBottomBar(
		current,
		_action.peerId,
		ForwardState::State::Downloading);
	_stepLifetime = std::make_unique<rpl::lifetime>();
	const auto self = shared_from_this();
	AyuSync::loadMedia(
		current,
		_currentUnit[_downloadIndex],
		*_stepLifetime,
		[self](bool success) {
			self->downloadFinished(success);
		});
}

void Operation::downloadFinished(bool success) {
	_stepLifetime.reset();
	if (!success) {
		finish(Result::Failure);
		return;
	} else if (_cancelRequested) {
		finish(Result::Cancelled);
		return;
	}
	++_downloadIndex;
	startDownload();
}

bool Operation::prepareCurrentUnit() {
	const auto current = session();
	if (!current) {
		return false;
	}
	auto list = Ui::PreparedList();
	list.files.reserve(_currentUnit.size());
	auto sendImagesAsPhotos = false;
	for (const auto itemId : _currentUnit) {
		const auto item = resolve(itemId);
		const auto media = item ? item->media() : nullptr;
		const auto photo = media ? media->photo() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (!item || (!photo && !document)) {
			return false;
		}
		auto prepared = Ui::PreparedFile(AyuSync::filePath(current, itemId));
		if (prepared.path.isEmpty()) {
			return false;
		}
		Storage::PrepareDetails(
			prepared,
			st::sendMediaPreviewSize,
			PhotoSideLimit());
		const auto info = QFileInfo(prepared.path);
		if (!info.isFile() || info.size() <= 0) {
			return false;
		}
		if (document
			&& document->size > 0
			&& info.size() != document->size) {
			return false;
		}
		prepared.caption = mediaCaption(itemId);
		list.files.push_back(std::move(prepared));
		sendImagesAsPhotos = sendImagesAsPhotos || (photo != nullptr);
	}
	auto way = Ui::SendFilesWay();
	way.setGroupFiles(true);
	way.setSendImagesAsPhotos(sendImagesAsPhotos);
	auto groups = Ui::DivideByGroups(
		std::move(list),
		way,
		current->data().history(_action.peerId)->peer->slowmodeApplied());
	_preparedGroups.clear();
	auto offset = 0;
	for (auto &group : groups) {
		const auto count = int(group.list.files.size());
		if (!count) {
			return false;
		}
		const auto first = resolve(_currentUnit[offset]);
		if (!first) {
			return false;
		}
		auto prepared = PreparedGroup{
			.list = std::move(group.list),
			.items = {},
			.type = sendImagesAsPhotos
				? SendMediaType::Photo
				: MediaType(first),
		};
		prepared.items.insert(
			prepared.items.end(),
			_currentUnit.begin() + offset,
			_currentUnit.begin() + offset + count);
		_preparedGroups.push_back(std::move(prepared));
		offset += count;
	}
	return offset == int(_currentUnit.size());
}

void Operation::sendNextPreparedGroup() {
	if (_cancelRequested) {
		finish(Result::Cancelled);
		return;
	} else if (_preparedIndex == int(_preparedGroups.size())) {
		unitFinished();
		return;
	}
	const auto current = session();
	if (!current) {
		finish(Result::Failure);
		return;
	}
	auto &prepared = _preparedGroups[_preparedIndex];
	const auto first = resolve(prepared.items.front());
	const auto media = first ? first->media() : nullptr;
	const auto document = media ? media->document() : nullptr;
	if (!first || !media) {
		finish(Result::Failure);
		return;
	}
	auto sendAction = takeUnitAction(int(prepared.items.size()));
	sendAction.options.invertCaption = first->invertMedia();
	const auto self = shared_from_this();
	const auto completion = Api::MakeSendCompletion([self](bool success) {
		self->preparedGroupFinished(success);
	});
	_state->updateBottomBar(
		current,
		_action.peerId,
		ForwardState::State::Sending);
	if (prepared.items.size() == 1 && document && document->sticker()) {
		auto message = Api::MessageToSend(sendAction);
		message.textWithTags = mediaCaption(prepared.items.front());
		message.completion = completion;
		Api::SendExistingDocument(
			std::move(message),
			document,
			std::nullopt);
	} else if (prepared.items.size() == 1
		&& document
		&& (document->isVoiceMessage() || document->isVideoMessage())) {
		const auto path = prepared.list.files.front().path;
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly)) {
			completion->fail();
			return;
		}
		current->api().sendVoiceMessage(
			file.readAll(),
			{},
			document->duration(),
			document->isVideoMessage(),
			sendAction,
			completion,
			mediaCaption(prepared.items.front()));
	} else {
		auto album = (prepared.list.files.size() > 1)
			? std::make_shared<SendingAlbum>()
			: std::shared_ptr<SendingAlbum>();
		if (album) {
			album->groupId = base::RandomValue<uint64>();
		}
		current->api().sendFiles(
			std::move(prepared.list),
			prepared.type,
			std::move(album),
			sendAction,
			completion);
	}
}

void Operation::preparedGroupFinished(bool success) {
	if (!success) {
		finish(Result::Failure);
		return;
	} else if (_cancelRequested) {
		finish(Result::Cancelled);
		return;
	}
	++_preparedIndex;
	if (_preparedGroups.empty()) {
		unitFinished();
	} else {
		sendNextPreparedGroup();
	}
}

void Operation::unitFinished() {
	_state->sentMessages += int(_currentUnit.size());
	startNextUnit();
}

void Operation::finish(Result result) {
	const auto guard = shared_from_this();
	if (_finished) {
		return;
	}
	_finished = true;
	if (_richRequest) {
		_richRequest->cancel();
		_richRequest.reset();
	}
	_stepLifetime.reset();
	const auto current = session();
	if (current) {
		_state->updateBottomBar(
			current,
			_action.peerId,
			ForwardState::State::Finished);
		OperationFinished(current, guard);
	}
	if (_completion) {
		base::take(_completion)(result);
	}
}

void Operation::finishDetached(Result result) {
	if (_finished) {
		return;
	}
	_finished = true;
	if (_richRequest) {
		_richRequest->cancel();
		_richRequest.reset();
	}
	_stepLifetime.reset();
	_state->state = ForwardState::State::Finished;
	if (_completion) {
		base::take(_completion)(result);
	}
}

std::shared_ptr<Operation> FindVisible(PeerId peerId) {
	for (const auto &entry : VisibleOperations) {
		const auto &visible = entry.second;
		const auto i = visible.find(peerId);
		if (i != visible.end()) {
			const auto operation = i->second.lock();
			if (operation) {
				return operation;
			}
		}
	}
	return {};
}

} // namespace

void ForwardState::updateBottomBar(
		not_null<Main::Session*> session,
		PeerId peerId,
		State newState) {
	state = newState;
	session->changes().peerUpdated(
		session->data().peer(peerId),
		Data::PeerUpdate::Flag::Rights);
}

bool isForwarding(const PeerId &id) {
	const auto operation = id ? FindVisible(id) : nullptr;
	return operation
		&& operation->state()->state != ForwardState::State::Finished;
}

void cancelForward(const PeerId &id, const Main::Session &session) {
	const auto i = Operations.find(const_cast<Main::Session*>(&session));
	if (i == Operations.end() || i->second.queue.empty()) {
		return;
	}
	const auto operation = i->second.queue.front();
	if (operation->peerId() == id) {
		operation->cancel();
	}
}

std::pair<QString, QString> stateName(const PeerId &id) {
	const auto operation = FindVisible(id);
	if (!operation) {
		return {};
	}
	const auto state = operation->state();
	const auto messages = tr::ayu_AyuForwardStatusSentCount(
		tr::now,
		lt_count1,
		QString::number(state->sentMessages),
		lt_count2,
		QString::number(state->totalMessages));
	const auto chunks = tr::ayu_AyuForwardStatusChunkCount(
		tr::now,
		lt_count1,
		QString::number(state->currentChunk + 1),
		lt_count2,
		QString::number(state->totalChunks));
	const auto progress = (state->totalChunks <= 1)
		? messages
		: messages + u" • "_q + chunks;
	switch (state->state) {
	case ForwardState::State::Preparing:
		return { tr::ayu_AyuForwardStatusPreparing(tr::now), progress };
	case ForwardState::State::Downloading:
		return { tr::ayu_AyuForwardStatusLoadingMedia(tr::now), QString() };
	case ForwardState::State::Sending:
		return { tr::ayu_AyuForwardStatusForwarding(tr::now), progress };
	case ForwardState::State::Finished:
		return { tr::ayu_AyuForwardStatusFinished(tr::now), progress };
	}
	Unexpected("State in AyuForward::stateName.");
}

bool isAyuForwardNeeded(
		const std::vector<not_null<HistoryItem*>> &items) {
	return ranges::any_of(items, [](not_null<HistoryItem*> item) {
		return isAyuForwardNeeded(item);
	});
}

bool isAyuForwardNeeded(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return item->isDeleted()
		|| item->isAyuNoForwards()
		|| item->unsupportedTTL()
		|| (media && media->ttlSeconds());
}

bool isFullAyuForwardNeeded(not_null<HistoryItem*> item) {
	const auto from = item->from();
	return (from && from->isAyuNoForwards())
		|| item->history()->peer->isAyuNoForwards();
}

void intelligentForward(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		const Data::ResolvedForwardDraft &draft,
		CompletionCallback completion) {
	Submit(
		session,
		action,
		draft,
		false,
		std::move(completion));
}

void forwardMessages(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		bool,
		const Data::ResolvedForwardDraft &draft,
		CompletionCallback completion) {
	Submit(
		session,
		action,
		draft,
		true,
		std::move(completion));
}

} // namespace AyuForward
