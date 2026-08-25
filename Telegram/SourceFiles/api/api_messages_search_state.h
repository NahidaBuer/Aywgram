/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

#include <optional>

namespace Api {

using SearchGeneration = uint64;

enum class SearchPage {
	First,
	More,
};

enum class SearchOutcomeType {
	Success,
	Empty,
	RpcFailure,
	Cancelled,
	Timeout,
};

enum class SearchBranch {
	Active,
	Migrated,
};

enum class SearchBranchState {
	Absent,
	Pending,
	Success,
	Empty,
	RpcFailure,
	Cancelled,
	Timeout,
};

struct FoundMessages {
	int total = -1;
	MessageIdsList messages;
	QString nextToken;
	bool hasMore = false;
	bool partial = false;
};

struct SearchCriteria {
	bool hasQuery = false;
	bool hasFrom = false;
	bool hasTags = false;
	bool hasTopic = false;
	bool hasFilter = false;
};

struct SearchDiagnostic {
	QString rpcType;
	int rpcCode = 0;
	bool hasQuery = false;
	bool hasFrom = false;
	bool hasTags = false;
	bool hasTopic = false;
	bool hasFilter = false;
};

struct SearchOutcome {
	SearchGeneration generation = 0;
	SearchPage page = SearchPage::First;
	SearchOutcomeType type = SearchOutcomeType::Cancelled;
	FoundMessages found;
	SearchDiagnostic diagnostic;

	[[nodiscard]] static SearchOutcome Success(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		FoundMessages found);
	[[nodiscard]] static SearchOutcome Empty(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		FoundMessages found = {});
	[[nodiscard]] static SearchOutcome FromFound(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		FoundMessages found);
	[[nodiscard]] static SearchOutcome FromRpcError(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		QString rpcType,
		int rpcCode);
	[[nodiscard]] static SearchOutcome RpcFailure(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		QString rpcType,
		int rpcCode);
	[[nodiscard]] static SearchOutcome Cancelled(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria);
	[[nodiscard]] static SearchOutcome Timeout(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria);
};

[[nodiscard]] SearchGeneration AllocateSearchGeneration();
[[nodiscard]] QString FormatSearchDiagnostic(
	const SearchDiagnostic &diagnostic);

class SearchOperationState final {
public:
	[[nodiscard]] std::optional<SearchOutcome> begin(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria);
	[[nodiscard]] std::optional<SearchOutcome> succeed(
		SearchGeneration generation,
		FoundMessages found);
	[[nodiscard]] std::optional<SearchOutcome> fail(
		SearchGeneration generation,
		QString rpcType,
		int rpcCode);
	[[nodiscard]] std::optional<SearchOutcome> cancel(
		SearchGeneration generation);
	[[nodiscard]] std::optional<SearchOutcome> timeout(
		SearchGeneration generation);
	[[nodiscard]] bool abandon(SearchGeneration generation);

	[[nodiscard]] bool pending() const;
	[[nodiscard]] bool isCurrent(SearchGeneration generation) const;
	[[nodiscard]] SearchGeneration generation() const;
	[[nodiscard]] SearchPage page() const;

private:
	[[nodiscard]] std::optional<SearchOutcome> finish(
		SearchGeneration generation,
		SearchOutcomeType type);

	SearchGeneration _generation = 0;
	SearchPage _page = SearchPage::First;
	SearchCriteria _criteria;
	bool _pending = false;

};

struct SearchCancelMask {
	bool active = false;
	bool migrated = false;
};

struct SearchMergedTransition {
	std::optional<SearchOutcome> outcome;
	SearchCancelMask cancel;
};

class SearchMergedState final {
public:
	[[nodiscard]] SearchMergedTransition begin(
		SearchGeneration generation,
		SearchPage page,
		SearchCriteria criteria,
		SearchGeneration activeGeneration,
		SearchGeneration migratedGeneration);
	[[nodiscard]] SearchMergedTransition accept(
		SearchBranch branch,
		const SearchOutcome &outcome);
	[[nodiscard]] SearchMergedTransition cancel(
		SearchGeneration generation);
	[[nodiscard]] SearchMergedTransition timeout(
		SearchGeneration generation);
	[[nodiscard]] SearchCancelMask abandon(SearchGeneration generation);

	[[nodiscard]] bool pending() const;
	[[nodiscard]] bool isCurrent(SearchGeneration generation) const;
	[[nodiscard]] SearchGeneration generation() const;
	[[nodiscard]] SearchPage page() const;
	[[nodiscard]] SearchGeneration childGeneration(
		SearchBranch branch) const;
	[[nodiscard]] SearchBranchState branchState(SearchBranch branch) const;

private:
	struct Branch {
		SearchGeneration generation = 0;
		SearchBranchState state = SearchBranchState::Absent;
		std::optional<SearchOutcome> outcome;
	};

	[[nodiscard]] SearchMergedTransition finish(
		SearchOutcomeType type,
		SearchCancelMask cancel);
	[[nodiscard]] SearchCancelMask pendingCancelMask() const;
	[[nodiscard]] Branch &branch(SearchBranch branch);
	[[nodiscard]] const Branch &branch(SearchBranch branch) const;

	SearchGeneration _generation = 0;
	SearchPage _page = SearchPage::First;
	SearchCriteria _criteria;
	Branch _active;
	Branch _migrated;
	bool _pending = false;

};

enum class SearchNextBranch {
	None,
	Active,
	Migrated,
};

struct SearchCombinedMessages {
	FoundMessages committed;
	FoundMessages heldMigrated;
	int activeTotal = 0;
	int migratedTotal = 0;
	int activeLoaded = 0;
	int migratedLoaded = 0;
	SearchNextBranch next = SearchNextBranch::None;
};

struct SearchPageCombination {
	FoundMessages delta;
	SearchCombinedMessages combined;
};

[[nodiscard]] SearchCombinedMessages CombineSearchFirstPage(
	FoundMessages active,
	std::optional<FoundMessages> migrated);
[[nodiscard]] SearchPageCombination CombineSearchPage(
	const SearchCombinedMessages &current,
	SearchBranch branch,
	FoundMessages page);

} // namespace Api
