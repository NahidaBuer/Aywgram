/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_messages_search_state.h"
#include "base/qt/qt_compare.h"
#include "base/timer.h"
#include "data/data_message_reaction_id.h"

class HistoryItem;
class History;
class PeerData;

namespace Data {
struct ReactionId;
} // namespace Data

namespace Api {

enum class SearchFilter {
	NoFilter,
	Photos,
	Videos,
	Files,
	Links,
	Music,
	VoiceMessages,
	VideoMessages,
	Gifs,
	Polls,
	MyMentions,
	Locations,
	Pinned,
};

enum class SearchSelectionChange {
	Sender,
	Filter,
};

struct SearchSelectionNormalization {
	bool clearSender = false;
	bool clearFilter = false;
};

[[nodiscard]] MTPMessagesFilter PrepareSearchFilter(SearchFilter filter);
[[nodiscard]] QString SearchFilterLabel(SearchFilter filter);
[[nodiscard]] const std::vector<SearchFilter> &SearchFilters();
[[nodiscard]] SearchSelectionNormalization NormalizeSearchSelection(
	SearchSelectionChange change,
	bool senderSelected,
	SearchFilter filter,
	bool exactIntersection);
[[nodiscard]] bool ShouldUseSearchIntersection(
	bool enabled,
	bool fixedFilter,
	bool senderSelected,
	SearchFilter filter);

class MessagesSearch final {
public:
	struct Request {
		QString query;
		PeerData *from = nullptr;
		std::vector<Data::ReactionId> tags;
		MsgId topMsgId;
		SearchFilter filter = SearchFilter::NoFilter;

		friend inline bool operator==(
			const Request &,
			const Request &) = default;
		friend inline auto operator<=>(
			const Request &,
			const Request &) = default;
	};

	explicit MessagesSearch(not_null<History*> history);
	~MessagesSearch();

	[[nodiscard]] SearchGeneration searchMessages(
		Request request,
		SearchGeneration generation = 0);
	[[nodiscard]] SearchGeneration searchMore(
		SearchGeneration generation = 0);
	void cancel();

	[[nodiscard]] SearchGeneration generation() const;
	[[nodiscard]] rpl::producer<SearchOutcome> outcomes() const;

private:
	using TLMessages = MTPmessages_Messages;

	[[nodiscard]] SearchGeneration start(
		Request request,
		MsgId offsetId,
		SearchGeneration generation,
		SearchPage page);
	[[nodiscard]] mtpRequestId sendRequest(
		Request request,
		MsgId offsetId,
		QString nextToken,
		SearchGeneration generation,
		SearchPage page,
		Fn<void()> finish);
	void searchReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		QString nextToken,
		SearchGeneration generation,
		SearchPage page,
		bool cached);
	void searchFailed(
		QString rpcType,
		int rpcCode,
		mtpRequestId requestId,
		SearchGeneration generation);
	int takeRequestOwnership();
	void publishTerminal(SearchOutcome outcome, int logicalId = 0);
	void timeout();
	void abandon();

	const not_null<History*> _history;

	base::flat_map<QString, TLMessages> _cacheOfStartByToken;

	Request _request;
	MsgId _offsetId;
	SearchOperationState _state;
	base::Timer _watchdog;

	int _searchInHistoryRequest = 0;
	mtpRequestId _requestId = 0;

	rpl::event_stream<SearchOutcome> _outcomes;

};

struct SearchIntersectionRequests {
	MessagesSearch::Request sender;
	MessagesSearch::Request filter;
};

[[nodiscard]] SearchIntersectionRequests PrepareSearchIntersectionRequests(
	const MessagesSearch::Request &request);

} // namespace Api
