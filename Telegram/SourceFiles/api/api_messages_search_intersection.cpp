/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_intersection.h"

#include "data/data_peer.h"
#include "history/history.h"

namespace Api {
namespace {

constexpr auto kIntersectionTimeout = crl::time(30000);

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

[[nodiscard]] bool IsSuccessful(SearchOutcomeType type) {
	return (type == SearchOutcomeType::Success)
		|| (type == SearchOutcomeType::Empty);
}

} // namespace

MessagesSearchIntersection::MessagesSearchIntersection(
		not_null<History*> history)
: _history(history)
, _senderSearch(history)
, _filterSearch(history)
, _watchdog([=] { timeout(); }) {
	_senderSearch.firstOutcomes(
	) | rpl::on_next([=](const SearchOutcome &outcome) {
		childOutcome(SearchIntersectionLeg::Sender, outcome);
	}, _lifetime);
	_senderSearch.nextOutcomes(
	) | rpl::on_next([=](const SearchOutcome &outcome) {
		childOutcome(SearchIntersectionLeg::Sender, outcome);
	}, _lifetime);
	_filterSearch.firstOutcomes(
	) | rpl::on_next([=](const SearchOutcome &outcome) {
		childOutcome(SearchIntersectionLeg::Filter, outcome);
	}, _lifetime);
	_filterSearch.nextOutcomes(
	) | rpl::on_next([=](const SearchOutcome &outcome) {
		childOutcome(SearchIntersectionLeg::Filter, outcome);
	}, _lifetime);
}

MessagesSearchIntersection::~MessagesSearchIntersection() {
	_lifetime.destroy();
	abandon();
}

void MessagesSearchIntersection::clear() {
	execute(_state.clear());
}

SearchGeneration MessagesSearchIntersection::search(
		const Request &request,
		SearchGeneration generation) {
	Expects(request.from != nullptr);
	Expects(request.filter != SearchFilter::NoFilter);
	if (!generation) {
		generation = AllocateSearchGeneration();
	} else if (generation == _state.generation()) {
		return 0;
	}
	_request = request;
	const auto migrated = (!_migratedDisabled && _history->migrateFrom())
		? _history->migrateFrom()->peer->id
		: PeerId();
	auto action = _state.begin(
		generation,
		CriteriaFromRequest(request),
		_history->peer->id,
		migrated);
	execute(std::move(action));
	if (_state.pending() && _state.generation() == generation) {
		_watchdog.callOnce(kIntersectionTimeout);
	}
	return (_state.generation() == generation) ? generation : 0;
}

SearchGeneration MessagesSearchIntersection::searchMore(
		SearchGeneration generation) {
	if (_state.pending() || !_state.canSearchMore()) {
		return 0;
	}
	if (!generation) {
		generation = AllocateSearchGeneration();
	} else if (generation == _state.generation()) {
		return 0;
	}
	auto action = _state.more(generation);
	execute(std::move(action));
	if (_state.pending() && _state.generation() == generation) {
		_watchdog.callOnce(kIntersectionTimeout);
	}
	return (_state.generation() == generation) ? generation : 0;
}

void MessagesSearchIntersection::disableMigrated() {
	clear();
	_migratedDisabled = true;
	_senderSearch.disableMigrated();
	_filterSearch.disableMigrated();
}

SearchGeneration MessagesSearchIntersection::generation() const {
	return _state.generation();
}

const FoundMessages &MessagesSearchIntersection::messages() const {
	return _state.messages();
}

auto MessagesSearchIntersection::request() const
-> const MessagesSearchIntersection::Request & {
	return _request;
}

auto MessagesSearchIntersection::firstOutcomes() const
-> rpl::producer<SearchOutcome> {
	return _firstOutcomes.events();
}

auto MessagesSearchIntersection::nextOutcomes() const
-> rpl::producer<SearchOutcome> {
	return _nextOutcomes.events();
}

void MessagesSearchIntersection::childOutcome(
		SearchIntersectionLeg leg,
		const SearchOutcome &outcome) {
	if (!_state.expects(leg, outcome.generation)) {
		return;
	}
	const auto &search = (leg == SearchIntersectionLeg::Sender)
		? _senderSearch
		: _filterSearch;
	const auto &found = search.messages();
	const auto exhausted = IsSuccessful(outcome.type)
		&& SearchIntersectionExhausted(found);
	execute(_state.accept(leg, outcome, exhausted));
}

void MessagesSearchIntersection::execute(SearchIntersectionAction action) {
	if (action.cancelSender) {
		_senderSearch.clear();
	}
	if (action.cancelFilter) {
		_filterSearch.clear();
	}
	if (action.outcome) {
		publish(std::move(*action.outcome));
	}
	if (action.senderRequest.generation) {
		executeRequest(
			SearchIntersectionLeg::Sender,
			action.senderRequest);
	}
	if (action.filterRequest.generation
		&& _state.expects(
			SearchIntersectionLeg::Filter,
			action.filterRequest.generation)) {
		executeRequest(
			SearchIntersectionLeg::Filter,
			action.filterRequest);
	}
}

void MessagesSearchIntersection::executeRequest(
		SearchIntersectionLeg leg,
		SearchIntersectionRequest request) {
	if (!_state.expects(leg, request.generation)) {
		return;
	}
	auto &search = (leg == SearchIntersectionLeg::Sender)
		? _senderSearch
		: _filterSearch;
	auto started = SearchGeneration(0);
	if (request.first) {
		const auto requests = PrepareSearchIntersectionRequests(_request);
		const auto &criterion = (leg == SearchIntersectionLeg::Sender)
			? requests.sender
			: requests.filter;
		started = search.search(criterion, request.generation);
	} else {
		started = search.searchMore(request.generation);
	}
	if (started != request.generation
		&& _state.expects(leg, request.generation)) {
		execute(_state.accept(
			leg,
			SearchOutcome::Cancelled(
				request.generation,
				request.first ? SearchPage::First : SearchPage::More,
				{}),
			true));
	}
}

void MessagesSearchIntersection::publish(SearchOutcome outcome) {
	_watchdog.cancel();
	if (outcome.page == SearchPage::First) {
		_firstOutcomes.fire(std::move(outcome));
	} else {
		_nextOutcomes.fire(std::move(outcome));
	}
}

void MessagesSearchIntersection::timeout() {
	execute(_state.timeout(_state.generation()));
}

void MessagesSearchIntersection::abandon() {
	_watchdog.cancel();
	execute(_state.abandon());
}

} // namespace Api
