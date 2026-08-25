/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_merged.h"

#include "history/history.h"

namespace Api {
namespace {

constexpr auto kSearchTimeout = crl::time(15000);

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

MessagesSearchMerged::MessagesSearchMerged(not_null<History*> history)
: _apiSearch(history)
, _watchdog([=] { timeout(); }) {
	if (const auto migrated = history->migrateFrom()) {
		_migratedSearch.emplace(migrated);
	}
	_apiSearch.outcomes() | rpl::on_next([=](const SearchOutcome &outcome) {
		childOutcome(SearchBranch::Active, outcome);
	}, _lifetime);
	if (_migratedSearch) {
		_migratedSearch->outcomes(
		) | rpl::on_next([=](const SearchOutcome &outcome) {
			childOutcome(SearchBranch::Migrated, outcome);
		}, _lifetime);
	}
}

MessagesSearchMerged::~MessagesSearchMerged() {
	_lifetime.destroy();
	abandon();
}

void MessagesSearchMerged::disableMigrated() {
	auto transition = _state.cancel(_state.generation());
	_watchdog.cancel();
	cancelChildren(transition.cancel);
	if (_migratedSearch) {
		_migratedSearch->cancel();
		_migratedSearch.reset();
	}
	_activeFirstFound.reset();
	_migratedFirstFound.reset();
	if (!_combined.heldMigrated.messages.empty()) {
		_combined.migratedLoaded = 0;
		_combined.migratedTotal = 0;
	} else {
		_combined.migratedTotal = _combined.migratedLoaded;
	}
	_combined.heldMigrated = {};
	_combined.committed.total = _combined.activeTotal
		+ _combined.migratedTotal;
	if (_combined.next == SearchNextBranch::Migrated) {
		_combined.next = SearchNextBranch::None;
	}
	_paginationClosed = (_combined.next == SearchNextBranch::None);
	if (transition.outcome) {
		publishOutcome(std::move(*transition.outcome));
	}
}

SearchGeneration MessagesSearchMerged::generation() const {
	return _state.generation();
}

const FoundMessages &MessagesSearchMerged::messages() const {
	return _combined.committed;
}

const MessagesSearch::Request &MessagesSearchMerged::request() const {
	return _request;
}

void MessagesSearchMerged::clear() {
	auto transition = _state.cancel(_state.generation());
	_watchdog.cancel();
	cancelChildren(transition.cancel);
	_activeFirstFound.reset();
	_migratedFirstFound.reset();
	_combined = {};
	_paginationClosed = true;
	if (transition.outcome) {
		publishOutcome(std::move(*transition.outcome));
	}
}

SearchGeneration MessagesSearchMerged::search(
		const Request &search,
		SearchGeneration generation) {
	if (!generation) {
		generation = AllocateSearchGeneration();
	} else if (generation == _state.generation()) {
		return 0;
	}
	const auto activeGeneration = AllocateSearchGeneration();
	const auto migratedGeneration = _migratedSearch
		? AllocateSearchGeneration()
		: 0;

	_request = search;
	_criteria = CriteriaFromRequest(search);
	_activeFirstFound.reset();
	_migratedFirstFound.reset();
	_combined = {};
	_paginationClosed = false;
	auto transition = _state.begin(
		generation,
		SearchPage::First,
		_criteria,
		activeGeneration,
		migratedGeneration);
	if (migratedGeneration) {
		_watchdog.callOnce(kSearchTimeout);
	} else {
		_watchdog.cancel();
	}
	cancelChildren(transition.cancel);
	if (transition.outcome) {
		publishOutcome(std::move(*transition.outcome));
	}
	if (!_state.isCurrent(generation)) {
		return (_state.generation() == generation) ? generation : 0;
	}

	const auto activeStarted = _apiSearch.searchMessages(
		search,
		activeGeneration);
	if (_state.generation() != generation) {
		return 0;
	}
	if (_state.pending() && activeStarted != activeGeneration) {
		childOutcome(
			SearchBranch::Active,
			SearchOutcome::Cancelled(
				activeGeneration,
				SearchPage::First,
				_criteria));
	}
	if (!_state.isCurrent(generation)) {
		return (_state.generation() == generation) ? generation : 0;
	}

	if (migratedGeneration) {
		const auto migratedStarted = _migratedSearch->searchMessages(
			search,
			migratedGeneration);
		if (_state.generation() != generation) {
			return 0;
		}
		if (_state.pending() && migratedStarted != migratedGeneration) {
			childOutcome(
				SearchBranch::Migrated,
				SearchOutcome::Cancelled(
					migratedGeneration,
					SearchPage::First,
					_criteria));
		}
	}
	return (_state.generation() == generation) ? generation : 0;
}

SearchGeneration MessagesSearchMerged::searchMore(
		SearchGeneration generation) {
	if (_state.pending()
		|| _paginationClosed
		|| _combined.next == SearchNextBranch::None) {
		return 0;
	}
	if (!generation) {
		generation = AllocateSearchGeneration();
	} else if (generation == _state.generation()) {
		return 0;
	}
	const auto branch = (_combined.next == SearchNextBranch::Active)
		? SearchBranch::Active
		: SearchBranch::Migrated;
	const auto childGeneration = AllocateSearchGeneration();
	const auto activeGeneration = (branch == SearchBranch::Active)
		? childGeneration
		: 0;
	const auto migratedGeneration = (branch == SearchBranch::Migrated)
		? childGeneration
		: 0;
	_watchdog.cancel();
	const auto transition = _state.begin(
		generation,
		SearchPage::More,
		_criteria,
		activeGeneration,
		migratedGeneration);
	Expects(!transition.outcome);
	Expects(!transition.cancel.active && !transition.cancel.migrated);

	const auto started = (branch == SearchBranch::Active)
		? _apiSearch.searchMore(childGeneration)
		: (_migratedSearch
			? _migratedSearch->searchMore(childGeneration)
			: 0);
	if (_state.generation() != generation) {
		return 0;
	}
	if (_state.pending() && started != childGeneration) {
		childOutcome(
			branch,
			SearchOutcome::Cancelled(
				childGeneration,
				SearchPage::More,
				_criteria));
	}
	return (_state.generation() == generation) ? generation : 0;
}

void MessagesSearchMerged::childOutcome(
		SearchBranch branch,
		const SearchOutcome &outcome) {
	if (!_state.pending()
		|| outcome.page != _state.page()
		|| outcome.generation != _state.childGeneration(branch)
		|| _state.branchState(branch) != SearchBranchState::Pending) {
		return;
	}
	const auto successful = IsSuccessful(outcome.type);
	auto transition = _state.accept(branch, outcome);
	if (successful && outcome.page == SearchPage::First) {
		if (branch == SearchBranch::Active) {
			_activeFirstFound = outcome.found;
		} else {
			_migratedFirstFound = outcome.found;
		}
	}
	if (!transition.outcome) {
		return;
	}

	_watchdog.cancel();
	auto mergedOutcome = std::move(*transition.outcome);
	if (successful && outcome.page == SearchPage::First) {
		Expects(_activeFirstFound.has_value());
		_combined = CombineSearchFirstPage(
			std::move(*_activeFirstFound),
			std::move(_migratedFirstFound));
		_paginationClosed = (_combined.next == SearchNextBranch::None);
		mergedOutcome = SearchOutcome::FromFound(
			_state.generation(),
			SearchPage::First,
			_criteria,
			_combined.committed);
	} else if (successful) {
		auto combined = CombineSearchPage(
			_combined,
			branch,
			outcome.found);
		_combined = std::move(combined.combined);
		_paginationClosed = (_combined.next == SearchNextBranch::None);
		mergedOutcome = SearchOutcome::FromFound(
			_state.generation(),
			SearchPage::More,
			_criteria,
			std::move(combined.delta));
	} else {
		_paginationClosed = true;
		if (outcome.page == SearchPage::First) {
			_combined = {};
		}
	}
	_activeFirstFound.reset();
	_migratedFirstFound.reset();
	cancelChildren(transition.cancel);
	publishOutcome(std::move(mergedOutcome));
}

void MessagesSearchMerged::cancelChildren(SearchCancelMask cancel) {
	if (cancel.active) {
		_apiSearch.cancel();
	}
	if (cancel.migrated && _migratedSearch) {
		_migratedSearch->cancel();
	}
}

void MessagesSearchMerged::publishOutcome(SearchOutcome outcome) {
	if (outcome.page == SearchPage::First) {
		_firstOutcomes.fire(std::move(outcome));
	} else {
		_nextOutcomes.fire(std::move(outcome));
	}
}

void MessagesSearchMerged::timeout() {
	const auto page = _state.page();
	auto transition = _state.timeout(_state.generation());
	if (!transition.outcome) {
		return;
	}
	_watchdog.cancel();
	_activeFirstFound.reset();
	_migratedFirstFound.reset();
	if (page == SearchPage::First) {
		_combined = {};
	}
	_paginationClosed = true;
	cancelChildren(transition.cancel);
	publishOutcome(std::move(*transition.outcome));
}

void MessagesSearchMerged::abandon() {
	const auto cancel = _state.abandon(_state.generation());
	_watchdog.cancel();
	cancelChildren(cancel);
	_apiSearch.cancel();
	if (_migratedSearch) {
		_migratedSearch->cancel();
	}
}

rpl::producer<SearchOutcome> MessagesSearchMerged::firstOutcomes() const {
	return _firstOutcomes.events();
}

rpl::producer<SearchOutcome> MessagesSearchMerged::nextOutcomes() const {
	return _nextOutcomes.events();
}

} // namespace Api
