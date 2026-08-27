/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_messages_search_intersection_state.h"
#include "api/api_messages_search_merged.h"
#include "base/timer.h"

class History;

namespace Api {

class MessagesSearchIntersection final {
public:
	using Request = MessagesSearch::Request;

	explicit MessagesSearchIntersection(not_null<History*> history);
	~MessagesSearchIntersection();

	void clear();
	[[nodiscard]] SearchGeneration search(
		const Request &request,
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
	struct DelayedRequest {
		SearchIntersectionLeg leg = SearchIntersectionLeg::Sender;
		SearchIntersectionRequest request;
	};

	void childOutcome(
		SearchIntersectionLeg leg,
		const SearchOutcome &outcome);
	[[nodiscard]] std::optional<MessageIdsList> localMatches(
		const FoundMessages &found) const;
	void execute(SearchIntersectionAction action);
	void executeRequest(
		SearchIntersectionLeg leg,
		SearchIntersectionRequest request);
	void publish(SearchOutcome outcome);
	void sendDelayedRequest();
	void timeout();
	void abandon();

	const not_null<History*> _history;
	MessagesSearchMerged _senderSearch;
	MessagesSearchMerged _filterSearch;
	SearchIntersectionState _state;
	Request _request;
	base::Timer _watchdog;
	base::Timer _requestThrottle;
	std::optional<DelayedRequest> _delayedRequest;
	crl::time _lastRequestAt = 0;
	bool _migratedDisabled = false;

	rpl::event_stream<SearchOutcome> _firstOutcomes;
	rpl::event_stream<SearchOutcome> _nextOutcomes;
	rpl::lifetime _lifetime;

};

} // namespace Api
