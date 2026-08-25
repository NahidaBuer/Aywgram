/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_state.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace Api {
namespace {

[[nodiscard]] SearchDiagnostic MakeDiagnostic(
		SearchCriteria criteria,
		QString rpcType = {},
		int rpcCode = 0) {
	return {
		.rpcType = std::move(rpcType),
		.rpcCode = rpcCode,
		.hasQuery = criteria.hasQuery,
		.hasFrom = criteria.hasFrom,
		.hasTags = criteria.hasTags,
		.hasTopic = criteria.hasTopic,
		.hasFilter = criteria.hasFilter,
	};
}

[[nodiscard]] bool IsValidRpcType(const QString &type) {
	if (type.isEmpty()) {
		return false;
	}
	for (const auto character : type) {
		const auto value = character.unicode();
		if ((value < 'A' || value > 'Z')
			&& (value < '0' || value > '9')
			&& value != '_') {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QString SanitizeRpcType(QString type) {
	return IsValidRpcType(type) ? std::move(type) : u"RPC_ERROR"_q;
}

[[nodiscard]] QString YesNo(bool value) {
	return value ? u"yes"_q : u"no"_q;
}

[[nodiscard]] SearchBranchState StateFor(SearchOutcomeType type) {
	switch (type) {
	case SearchOutcomeType::Success:
		return SearchBranchState::Success;
	case SearchOutcomeType::Empty:
		return SearchBranchState::Empty;
	case SearchOutcomeType::RpcFailure:
		return SearchBranchState::RpcFailure;
	case SearchOutcomeType::Cancelled:
		return SearchBranchState::Cancelled;
	case SearchOutcomeType::Timeout:
		return SearchBranchState::Timeout;
	}
	Unexpected("SearchOutcomeType in StateFor.");
}

[[nodiscard]] bool IsSuccessful(SearchOutcomeType type) {
	return (type == SearchOutcomeType::Success)
		|| (type == SearchOutcomeType::Empty);
}

[[nodiscard]] SearchOutcome RemapOutcome(
		const SearchOutcome &outcome,
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria) {
	switch (outcome.type) {
	case SearchOutcomeType::Success:
		return SearchOutcome::Success(
			generation,
			page,
			criteria,
			outcome.found);
	case SearchOutcomeType::Empty:
		return SearchOutcome::Empty(
			generation,
			page,
			criteria,
			outcome.found);
	case SearchOutcomeType::RpcFailure:
		return SearchOutcome::FromRpcError(
			generation,
			page,
			criteria,
			outcome.diagnostic.rpcType,
			outcome.diagnostic.rpcCode);
	case SearchOutcomeType::Cancelled:
		return SearchOutcome::Cancelled(generation, page, criteria);
	case SearchOutcomeType::Timeout:
		return SearchOutcome::Timeout(generation, page, criteria);
	}
	Unexpected("SearchOutcomeType in RemapOutcome.");
}

[[nodiscard]] int LoadedCount(const FoundMessages &found) {
	return int(found.messages.size());
}

void AppendMessages(MessageIdsList &to, const MessageIdsList &from) {
	to.insert(end(to), begin(from), end(from));
}

[[nodiscard]] SearchNextBranch NextBranch(
		int activeLoaded,
		int activeTotal,
		int migratedLoaded,
		int migratedTotal) {
	if (activeLoaded < activeTotal) {
		return SearchNextBranch::Active;
	} else if (migratedLoaded < migratedTotal) {
		return SearchNextBranch::Migrated;
	}
	return SearchNextBranch::None;
}

} // namespace

SearchOutcome SearchOutcome::Success(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		FoundMessages found) {
	Expects(generation != 0);
	Expects(!found.messages.empty());
	return {
		.generation = generation,
		.page = page,
		.type = SearchOutcomeType::Success,
		.found = std::move(found),
		.diagnostic = MakeDiagnostic(criteria),
	};
}

SearchOutcome SearchOutcome::Empty(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		FoundMessages found) {
	Expects(generation != 0);
	Expects(found.messages.empty());
	if (page == SearchPage::First || found.total < 0) {
		found.total = 0;
	}
	return {
		.generation = generation,
		.page = page,
		.type = SearchOutcomeType::Empty,
		.found = std::move(found),
		.diagnostic = MakeDiagnostic(criteria),
	};
}

SearchOutcome SearchOutcome::FromFound(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		FoundMessages found) {
	return found.messages.empty()
		? Empty(generation, page, criteria, std::move(found))
		: Success(generation, page, criteria, std::move(found));
}

SearchOutcome SearchOutcome::FromRpcError(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		QString rpcType,
		int rpcCode) {
	Expects(generation != 0);
	if (rpcType == u"SEARCH_QUERY_EMPTY"_q) {
		return Empty(generation, page, criteria);
	}
	return RpcFailure(
		generation,
		page,
		criteria,
		std::move(rpcType),
		rpcCode);
}

SearchOutcome SearchOutcome::RpcFailure(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		QString rpcType,
		int rpcCode) {
	Expects(generation != 0);
	Expects(rpcType != u"SEARCH_QUERY_EMPTY"_q);
	return {
		.generation = generation,
		.page = page,
		.type = SearchOutcomeType::RpcFailure,
		.found = {},
		.diagnostic = MakeDiagnostic(
			criteria,
			SanitizeRpcType(std::move(rpcType)),
			rpcCode),
	};
}

SearchOutcome SearchOutcome::Cancelled(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria) {
	Expects(generation != 0);
	return {
		.generation = generation,
		.page = page,
		.type = SearchOutcomeType::Cancelled,
		.found = {},
		.diagnostic = MakeDiagnostic(criteria),
	};
}

SearchOutcome SearchOutcome::Timeout(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria) {
	Expects(generation != 0);
	return {
		.generation = generation,
		.page = page,
		.type = SearchOutcomeType::Timeout,
		.found = {},
		.diagnostic = MakeDiagnostic(criteria),
	};
}

SearchGeneration AllocateSearchGeneration() {
	static auto last = std::atomic<SearchGeneration>(0);
	while (true) {
		const auto result = last.fetch_add(1, std::memory_order_relaxed) + 1;
		if (result != 0) {
			return result;
		}
	}
}

QString FormatSearchDiagnostic(const SearchDiagnostic &diagnostic) {
	return u"rpcType="_q
		+ SanitizeRpcType(diagnostic.rpcType)
		+ u" rpcCode="_q
		+ QString::number(diagnostic.rpcCode)
		+ u" hasQuery="_q
		+ YesNo(diagnostic.hasQuery)
		+ u" hasFrom="_q
		+ YesNo(diagnostic.hasFrom)
		+ u" hasTags="_q
		+ YesNo(diagnostic.hasTags)
		+ u" hasTopic="_q
		+ YesNo(diagnostic.hasTopic)
		+ u" hasFilter="_q
		+ YesNo(diagnostic.hasFilter);
}

std::optional<SearchOutcome> SearchOperationState::begin(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria) {
	Expects(generation != 0);
	Expects(!_pending || generation != _generation);
	auto cancelled = _pending
		? std::make_optional(SearchOutcome::Cancelled(
			_generation,
			_page,
			_criteria))
		: std::nullopt;
	_generation = generation;
	_page = page;
	_criteria = criteria;
	_pending = true;
	return cancelled;
}

std::optional<SearchOutcome> SearchOperationState::succeed(
		SearchGeneration generation,
		FoundMessages found) {
	if (!isCurrent(generation)) {
		return std::nullopt;
	}
	_pending = false;
	return SearchOutcome::FromFound(
		generation,
		_page,
		_criteria,
		std::move(found));
}

std::optional<SearchOutcome> SearchOperationState::fail(
		SearchGeneration generation,
		QString rpcType,
		int rpcCode) {
	if (!isCurrent(generation)) {
		return std::nullopt;
	}
	_pending = false;
	return SearchOutcome::FromRpcError(
		generation,
		_page,
		_criteria,
		std::move(rpcType),
		rpcCode);
}

std::optional<SearchOutcome> SearchOperationState::cancel(
		SearchGeneration generation) {
	return finish(generation, SearchOutcomeType::Cancelled);
}

std::optional<SearchOutcome> SearchOperationState::timeout(
		SearchGeneration generation) {
	return finish(generation, SearchOutcomeType::Timeout);
}

bool SearchOperationState::abandon(SearchGeneration generation) {
	if (!isCurrent(generation)) {
		return false;
	}
	_pending = false;
	return true;
}

bool SearchOperationState::pending() const {
	return _pending;
}

bool SearchOperationState::isCurrent(SearchGeneration generation) const {
	return _pending && generation && (generation == _generation);
}

SearchGeneration SearchOperationState::generation() const {
	return _generation;
}

SearchPage SearchOperationState::page() const {
	return _page;
}

std::optional<SearchOutcome> SearchOperationState::finish(
		SearchGeneration generation,
		SearchOutcomeType type) {
	if (!isCurrent(generation)) {
		return std::nullopt;
	}
	_pending = false;
	return (type == SearchOutcomeType::Cancelled)
		? SearchOutcome::Cancelled(generation, _page, _criteria)
		: SearchOutcome::Timeout(generation, _page, _criteria);
}

SearchMergedTransition SearchMergedState::begin(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		SearchGeneration activeGeneration,
		SearchGeneration migratedGeneration) {
	Expects(generation != 0);
	Expects(activeGeneration || migratedGeneration);
	Expects(!activeGeneration
		|| !migratedGeneration
		|| activeGeneration != migratedGeneration);
	Expects(!_pending || generation != _generation);
	auto result = SearchMergedTransition();
	if (_pending) {
		result.outcome = SearchOutcome::Cancelled(
			_generation,
			_page,
			_criteria);
		result.cancel = pendingCancelMask();
	}
	_generation = generation;
	_page = page;
	_criteria = criteria;
	_active = {
		.generation = activeGeneration,
		.state = activeGeneration
			? SearchBranchState::Pending
			: SearchBranchState::Absent,
	};
	_migrated = {
		.generation = migratedGeneration,
		.state = migratedGeneration
			? SearchBranchState::Pending
			: SearchBranchState::Absent,
	};
	_pending = true;
	return result;
}

SearchMergedTransition SearchMergedState::accept(
		SearchBranch which,
		const SearchOutcome &outcome) {
	if (!_pending || outcome.page != _page) {
		return {};
	}
	auto &accepted = branch(which);
	if (accepted.state != SearchBranchState::Pending
		|| outcome.generation != accepted.generation) {
		return {};
	}
	accepted.state = StateFor(outcome.type);
	accepted.outcome = outcome;
	if (!IsSuccessful(outcome.type)) {
		auto mask = pendingCancelMask();
		if (mask.active) {
			_active.state = SearchBranchState::Cancelled;
		}
		if (mask.migrated) {
			_migrated.state = SearchBranchState::Cancelled;
		}
		_pending = false;
		return {
			.outcome = RemapOutcome(
				outcome,
				_generation,
				_page,
				_criteria),
			.cancel = mask,
		};
	}
	if (_active.state == SearchBranchState::Pending
		|| _migrated.state == SearchBranchState::Pending) {
		return {};
	}
	auto found = FoundMessages();
	if (_active.outcome) {
		if (_migrated.outcome) {
			found = CombineSearchFirstPage(
				_active.outcome->found,
				_migrated.outcome->found).committed;
		} else {
			found = _active.outcome->found;
		}
	} else {
		found = _migrated.outcome->found;
	}
	_pending = false;
	return {
		.outcome = SearchOutcome::FromFound(
			_generation,
			_page,
			_criteria,
			std::move(found)),
	};
}

SearchMergedTransition SearchMergedState::cancel(
		SearchGeneration generation) {
	if (!isCurrent(generation)) {
		return {};
	}
	return finish(SearchOutcomeType::Cancelled, pendingCancelMask());
}

SearchMergedTransition SearchMergedState::timeout(
		SearchGeneration generation) {
	if (!isCurrent(generation)) {
		return {};
	}
	return finish(SearchOutcomeType::Timeout, pendingCancelMask());
}

SearchCancelMask SearchMergedState::abandon(SearchGeneration generation) {
	if (!isCurrent(generation)) {
		return {};
	}
	const auto result = pendingCancelMask();
	if (result.active) {
		_active.state = SearchBranchState::Cancelled;
	}
	if (result.migrated) {
		_migrated.state = SearchBranchState::Cancelled;
	}
	_pending = false;
	return result;
}

bool SearchMergedState::pending() const {
	return _pending;
}

bool SearchMergedState::isCurrent(SearchGeneration generation) const {
	return _pending && generation && (generation == _generation);
}

SearchGeneration SearchMergedState::generation() const {
	return _generation;
}

SearchPage SearchMergedState::page() const {
	return _page;
}

SearchGeneration SearchMergedState::childGeneration(SearchBranch which) const {
	return branch(which).generation;
}

SearchBranchState SearchMergedState::branchState(SearchBranch which) const {
	return branch(which).state;
}

SearchMergedTransition SearchMergedState::finish(
		SearchOutcomeType type,
		SearchCancelMask cancel) {
	const auto state = StateFor(type);
	if (cancel.active) {
		_active.state = state;
	}
	if (cancel.migrated) {
		_migrated.state = state;
	}
	_pending = false;
	return {
		.outcome = (type == SearchOutcomeType::Cancelled)
			? SearchOutcome::Cancelled(_generation, _page, _criteria)
			: SearchOutcome::Timeout(_generation, _page, _criteria),
		.cancel = cancel,
	};
}

SearchCancelMask SearchMergedState::pendingCancelMask() const {
	return {
		.active = (_active.state == SearchBranchState::Pending),
		.migrated = (_migrated.state == SearchBranchState::Pending),
	};
}

SearchMergedState::Branch &SearchMergedState::branch(SearchBranch which) {
	return (which == SearchBranch::Active) ? _active : _migrated;
}

const SearchMergedState::Branch &SearchMergedState::branch(
		SearchBranch which) const {
	return (which == SearchBranch::Active) ? _active : _migrated;
}

SearchCombinedMessages CombineSearchFirstPage(
		FoundMessages active,
		std::optional<FoundMessages> migrated) {
	Expects(active.total >= 0);
	Expects(!migrated || migrated->total >= 0);
	auto result = SearchCombinedMessages();
	result.activeTotal = active.total;
	result.activeLoaded = LoadedCount(active);
	result.committed = std::move(active);
	if (migrated) {
		result.migratedTotal = migrated->total;
		result.migratedLoaded = LoadedCount(*migrated);
		result.committed.total += migrated->total;
		if (result.activeLoaded >= result.activeTotal) {
			AppendMessages(
				result.committed.messages,
				migrated->messages);
			result.committed.nextToken = migrated->nextToken;
		} else {
			result.heldMigrated = std::move(*migrated);
		}
	}
	result.next = NextBranch(
		result.activeLoaded,
		result.activeTotal,
		result.migratedLoaded,
		result.migratedTotal);
	return result;
}

SearchPageCombination CombineSearchPage(
		const SearchCombinedMessages &current,
		SearchBranch branch,
		FoundMessages page) {
	Expects(page.total >= 0);
	auto result = SearchPageCombination{ .combined = current };
	result.delta = page;
	if (branch == SearchBranch::Active) {
		Expects(current.next == SearchNextBranch::Active);
		result.combined.activeTotal = std::max(
			current.activeTotal,
			page.total);
		result.combined.activeLoaded = page.messages.empty()
			? result.combined.activeTotal
			: (result.combined.activeLoaded + LoadedCount(page));
		AppendMessages(
			result.combined.committed.messages,
			page.messages);
		result.combined.committed.nextToken = page.nextToken;
		if (result.combined.activeLoaded >= result.combined.activeTotal
			&& !result.combined.heldMigrated.messages.empty()) {
			AppendMessages(
				result.delta.messages,
				result.combined.heldMigrated.messages);
			AppendMessages(
				result.combined.committed.messages,
				result.combined.heldMigrated.messages);
			result.combined.committed.nextToken
				= result.combined.heldMigrated.nextToken;
			result.combined.heldMigrated = {};
		}
	} else {
		Expects(current.next == SearchNextBranch::Migrated);
		result.combined.migratedTotal = std::max(
			current.migratedTotal,
			page.total);
		result.combined.migratedLoaded = page.messages.empty()
			? result.combined.migratedTotal
			: (result.combined.migratedLoaded + LoadedCount(page));
		AppendMessages(
			result.combined.committed.messages,
			page.messages);
		result.combined.committed.nextToken = page.nextToken;
	}
	result.combined.committed.total = result.combined.activeTotal
		+ result.combined.migratedTotal;
	result.delta.total = result.combined.committed.total;
	result.combined.next = NextBranch(
		result.combined.activeLoaded,
		result.combined.activeTotal,
		result.combined.migratedLoaded,
		result.combined.migratedTotal);
	return result;
}

} // namespace Api
