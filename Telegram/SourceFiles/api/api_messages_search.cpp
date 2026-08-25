/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_message_reaction_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kSearchPerPage = 50;
constexpr auto kSearchTimeout = crl::time(15000);

[[nodiscard]] MessageIdsList HistoryItemsFromTL(
		not_null<Data::Session*> data,
		const QVector<MTPMessage> &messages) {
	auto result = MessageIdsList();
	for (const auto &message : messages) {
		const auto peerId = PeerFromMessage(message);
		if (data->peerLoaded(peerId)) {
			if (DateFromMessage(message)) {
				const auto item = data->addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);
				result.push_back(item->fullId());
			}
		} else {
			LOG(("API Error: search result has an unavailable peer."));
		}
	}
	return result;
}

[[nodiscard]] SearchCriteria CriteriaFromRequest(
		const MessagesSearch::Request &request) {
	return {
		.hasQuery = !request.query.isEmpty(),
		.hasFrom = (request.from != nullptr),
		.hasTags = !request.tags.empty(),
		.hasTopic = bool(request.topMsgId),
		.hasFilter = (request.filter != SearchFilter::NoFilter),
	};
}

[[nodiscard]] QString RequestToToken(
		const MessagesSearch::Request &request) {
	auto result = request.query;
	if (request.from) {
		result += '\n' + QString::number(request.from->id.value);
	}
	for (const auto &tag : request.tags) {
		result += '\n';
		if (const auto customId = tag.custom()) {
			result += u"custom"_q + QString::number(customId);
		} else {
			result += u"emoji"_q + tag.emoji();
		}
	}
	if (request.topMsgId) {
		result += u"\ntop"_q + QString::number(request.topMsgId.bare);
	}
	if (request.filter != SearchFilter::NoFilter) {
		result += u"\nfilter"_q
			+ QString::number(static_cast<int>(request.filter));
	}
	return result;
}

[[nodiscard]] FoundMessages SearchResultFromTL(
		not_null<History*> history,
		const MTPmessages_Messages &result,
		const QString &nextToken,
		bool cached) {
	auto &owner = history->owner();
	return result.match([&](const MTPDmessages_messages &data) {
		if (!cached) {
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
			history->peer->processTopics(data.vtopics());
		}
		auto items = HistoryItemsFromTL(&owner, data.vmessages().v);
		const auto total = int(data.vmessages().v.size());
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_messagesSlice &data) {
		if (!cached) {
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
			history->peer->processTopics(data.vtopics());
		}
		auto items = HistoryItemsFromTL(&owner, data.vmessages().v);
		const auto total = int(data.vcount().v);
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_channelMessages &data) {
		if (!cached) {
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
			if (const auto channel = history->peer->asChannel()) {
				channel->ptsReceived(data.vpts().v);
			} else {
				LOG(("API Error: search returned channel messages "
					"for a non-channel peer."));
			}
			history->peer->processTopics(data.vtopics());
		}
		auto items = HistoryItemsFromTL(&owner, data.vmessages().v);
		const auto total = int(data.vcount().v);
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_messagesNotModified &) {
		return FoundMessages{ 0, {}, nextToken };
	});
}

} // namespace

QString SearchFilterLabel(SearchFilter filter) {
	switch (filter) {
	case SearchFilter::NoFilter:
		return tr::ayu_SearchFilterAll(tr::now);
	case SearchFilter::Photos:
		return tr::lng_media_type_photos(tr::now);
	case SearchFilter::Videos:
		return tr::lng_media_type_videos(tr::now);
	case SearchFilter::Files:
		return tr::lng_media_type_files(tr::now);
	case SearchFilter::Links:
		return tr::lng_media_type_links(tr::now);
	case SearchFilter::Music:
		return tr::lng_media_type_songs(tr::now);
	case SearchFilter::VoiceMessages:
		return tr::lng_media_type_audios(tr::now);
	case SearchFilter::VideoMessages:
		return tr::lng_media_type_rounds(tr::now);
	case SearchFilter::Gifs:
		return tr::lng_media_type_gifs(tr::now);
	case SearchFilter::Polls:
		return tr::lng_media_type_polls(tr::now);
	case SearchFilter::MyMentions:
		return tr::ayu_SearchFilterMentions(tr::now);
	case SearchFilter::Locations:
		return tr::lng_maps_point(tr::now);
	case SearchFilter::Pinned:
		return tr::lng_settings_events_pinned(tr::now);
	}
	Unexpected("SearchFilter in SearchFilterLabel.");
}

MessagesSearch::MessagesSearch(not_null<History*> history)
: _history(history)
, _watchdog([=] { timeout(); }) {
}

MessagesSearch::~MessagesSearch() {
	abandon();
}

SearchGeneration MessagesSearch::searchMessages(
		Request request,
		SearchGeneration generation) {
	return start(std::move(request), MsgId(), generation, SearchPage::First);
}

SearchGeneration MessagesSearch::searchMore(
		SearchGeneration generation) {
	if (_state.pending()
		|| _searchInHistoryRequest
		|| _requestId
		|| !_state.generation()
		|| !_offsetId) {
		return 0;
	}
	return start(_request, _offsetId, generation, SearchPage::More);
}

SearchGeneration MessagesSearch::start(
		Request request,
		MsgId offsetId,
		SearchGeneration generation,
		SearchPage page) {
	if (!generation) {
		generation = AllocateSearchGeneration();
	} else if (generation == _state.generation()) {
		return 0;
	}

	const auto criteria = CriteriaFromRequest(request);
	const auto nextToken = RequestToToken(request);
	const auto oldLogicalId = takeRequestOwnership();
	const auto cancelled = _state.begin(generation, page, criteria);
	_request = std::move(request);
	_offsetId = offsetId;

	if (cancelled) {
		publishTerminal(*cancelled, oldLogicalId);
		if (!_state.isCurrent(generation)) {
			return 0;
		}
	} else {
		_history->owner().histories().cancelRequest(oldLogicalId);
	}

	if (page == SearchPage::First) {
		const auto it = _cacheOfStartByToken.find(nextToken);
		if (it != end(_cacheOfStartByToken)) {
			searchReceived(
				it->second,
				0,
				nextToken,
				generation,
				page,
				true);
			return generation;
		}
	}

	_watchdog.callOnce(kSearchTimeout);
	const auto requestCopy = _request;
	auto generator = [=](Fn<void()> finish) {
		if (!_state.isCurrent(generation)) {
			return mtpRequestId(0);
		}
		return sendRequest(
			requestCopy,
			offsetId,
			nextToken,
			generation,
			page,
			std::move(finish));
	};
	const auto logicalId = _history->owner().histories().sendRequest(
		_history,
		Data::Histories::RequestType::History,
		std::move(generator));
	if (_state.isCurrent(generation)) {
		_searchInHistoryRequest = logicalId;
	} else {
		_history->owner().histories().cancelRequest(logicalId);
	}
	return generation;
}

mtpRequestId MessagesSearch::sendRequest(
		Request request,
		MsgId offsetId,
		QString nextToken,
		SearchGeneration generation,
		SearchPage page,
		Fn<void()> finish) {
	using Flag = MTPmessages_Search::Flag;
	const auto from = request.from;
	const auto fromPeer = _history->peer->isUser() ? nullptr : from;
	const auto savedPeer = _history->peer->isSelf() ? from : nullptr;
	const auto requestId = _history->session().api().request(
		MTPmessages_Search(
			MTP_flags((fromPeer ? Flag::f_from_id : Flag())
				| (savedPeer ? Flag::f_saved_peer_id : Flag())
				| (request.topMsgId ? Flag::f_top_msg_id : Flag())
				| (request.tags.empty()
					? Flag()
					: Flag::f_saved_reaction)),
			_history->peer->input(),
			MTP_string(request.query),
			(fromPeer ? fromPeer->input() : MTP_inputPeerEmpty()),
			(savedPeer ? savedPeer->input() : MTP_inputPeerEmpty()),
			MTP_vector_from_range(
				request.tags | ranges::views::transform(
					Data::ReactionToMTP)),
			MTP_int(request.topMsgId),
			PrepareSearchFilter(request.filter),
			MTP_int(0),
			MTP_int(0),
			MTP_int(offsetId),
			MTP_int(0),
			MTP_int(kSearchPerPage),
			MTP_int(0),
			MTP_int(0),
			MTP_long(0)
	)).done([=](const TLMessages &result, mtpRequestId requestId) {
		finish();
		searchReceived(
			result,
			requestId,
			nextToken,
			generation,
			page,
			false);
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		finish();
		searchFailed(
			error.type(),
			error.code(),
			requestId,
			generation);
	}).handleAllErrors().send();
	if (_state.isCurrent(generation)) {
		_requestId = requestId;
	}
	return requestId;
}

void MessagesSearch::searchReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		QString nextToken,
		SearchGeneration generation,
		SearchPage page,
		bool cached) {
	if (!_state.isCurrent(generation) || requestId != _requestId) {
		return;
	}
	takeRequestOwnership();

	auto found = SearchResultFromTL(_history, result, nextToken, cached);
	if (!_state.isCurrent(generation)) {
		return;
	}
	auto outcome = _state.succeed(generation, std::move(found));
	if (!outcome) {
		return;
	}
	_offsetId = outcome->found.messages.empty()
		? MsgId()
		: outcome->found.messages.back().msg;
	if (!cached && page == SearchPage::First) {
		_cacheOfStartByToken.emplace(nextToken, result);
	}
	publishTerminal(std::move(*outcome));
}

void MessagesSearch::searchFailed(
		QString rpcType,
		int rpcCode,
		mtpRequestId requestId,
		SearchGeneration generation) {
	if (!_state.isCurrent(generation) || requestId != _requestId) {
		return;
	}
	takeRequestOwnership();
	auto outcome = _state.fail(
		generation,
		std::move(rpcType),
		rpcCode);
	if (!outcome) {
		return;
	}
	if (outcome->type == SearchOutcomeType::Empty) {
		_offsetId = {};
	}
	publishTerminal(std::move(*outcome));
}

void MessagesSearch::cancel() {
	auto outcome = _state.cancel(_state.generation());
	if (!outcome) {
		return;
	}
	const auto logicalId = takeRequestOwnership();
	publishTerminal(std::move(*outcome), logicalId);
}

void MessagesSearch::timeout() {
	auto outcome = _state.timeout(_state.generation());
	if (!outcome) {
		return;
	}
	const auto logicalId = takeRequestOwnership();
	publishTerminal(std::move(*outcome), logicalId);
}

int MessagesSearch::takeRequestOwnership() {
	_watchdog.cancel();
	const auto logicalId = base::take(_searchInHistoryRequest);
	_requestId = 0;
	return logicalId;
}

void MessagesSearch::publishTerminal(
		SearchOutcome outcome,
		int logicalId) {
	_history->owner().histories().cancelRequest(logicalId);
	_outcomes.fire(std::move(outcome));
}

void MessagesSearch::abandon() {
	(void)_state.abandon(_state.generation());
	const auto logicalId = takeRequestOwnership();
	_history->owner().histories().cancelRequest(logicalId);
}

SearchGeneration MessagesSearch::generation() const {
	return _state.generation();
}

rpl::producer<SearchOutcome> MessagesSearch::outcomes() const {
	return _outcomes.events();
}

} // namespace Api
