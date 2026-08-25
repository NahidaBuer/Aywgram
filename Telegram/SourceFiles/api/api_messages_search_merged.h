/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_messages_search.h"

class History;
class PeerData;

namespace Data {
struct ReactionId;
} // namespace Data

namespace Api {

// Search in both of history and migrated history, if it exists.
class MessagesSearchMerged final {
public:
	using Request = MessagesSearch::Request;
	using CachedRequests = base::flat_set<Request>;

	MessagesSearchMerged(not_null<History*> history);
	~MessagesSearchMerged();

	void clear();
	[[nodiscard]] SearchGeneration search(
		const Request &search,
		SearchGeneration generation = 0);
	[[nodiscard]] SearchGeneration searchMore(
		SearchGeneration generation = 0);
	void disableMigrated();

	[[nodiscard]] SearchGeneration generation() const;
	[[nodiscard]] const FoundMessages &messages() const;
	[[nodiscard]] const Request &request() const;

	[[nodiscard]] rpl::producer<SearchOutcome> firstOutcomes() const;
	[[nodiscard]] rpl::producer<SearchOutcome> nextOutcomes() const;

private:
	void childOutcome(SearchBranch branch, const SearchOutcome &outcome);
	void cancelChildren(SearchCancelMask cancel);
	void publishOutcome(SearchOutcome outcome);
	void timeout();
	void abandon();

	MessagesSearch _apiSearch;
	Request _request;

	std::optional<MessagesSearch> _migratedSearch;
	std::optional<FoundMessages> _activeFirstFound;
	std::optional<FoundMessages> _migratedFirstFound;

	SearchCriteria _criteria;
	SearchMergedState _state;
	SearchCombinedMessages _combined;
	base::Timer _watchdog;

	bool _paginationClosed = true;

	rpl::event_stream<SearchOutcome> _firstOutcomes;
	rpl::event_stream<SearchOutcome> _nextOutcomes;

	rpl::lifetime _lifetime;

};

} // namespace Api
