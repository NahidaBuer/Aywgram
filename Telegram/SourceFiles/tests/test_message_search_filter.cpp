/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search.h"

#include "api/api_messages_search_intersection_state.h"
#include "api/api_messages_search_state.h"
#include "base/qt/qt_common_adapters.h"

#include <QtCore/QCoreApplication>

#include <array>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

[[nodiscard]] bool Check(bool condition, const char *message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}

[[nodiscard]] MessageIdsList MakeIds(
		std::initializer_list<int64> values) {
	auto result = MessageIdsList();
	for (const auto value : values) {
		result.emplace_back(PeerId(UserId(1)), MsgId(value));
	}
	return result;
}

[[nodiscard]] MessageIdsList MakePeerIds(
		int64 peer,
		std::initializer_list<int64> values) {
	auto result = MessageIdsList();
	for (const auto value : values) {
		result.emplace_back(PeerId(UserId(peer)), MsgId(value));
	}
	return result;
}

[[nodiscard]] Api::FoundMessages MakeFound(
		int total,
		std::initializer_list<int64> values,
		QString nextToken = {}) {
	auto result = Api::FoundMessages();
	result.total = total;
	result.messages = MakeIds(values);
	result.nextToken = std::move(nextToken);
	return result;
}

[[nodiscard]] Api::FoundMessages MakeFoundWithTraits(
		int total,
		std::initializer_list<int64> values,
		PeerId sender,
		Api::LocalSearchMessageFlags flags,
		QString nextToken = {}) {
	auto result = MakeFound(total, values, std::move(nextToken));
	for (const auto id : result.messages) {
		result.traits.insert_or_assign(id, Api::SearchMessageTraits{
			.sender = sender,
			.filterFlags = flags,
		});
	}
	return result;
}

[[nodiscard]] bool HasTraits(
		const Api::FoundMessages &found,
		FullMsgId id,
		Api::SearchMessageTraits expected) {
	const auto i = found.traits.find(id);
	return i != end(found.traits) && i->second == expected;
}

[[nodiscard]] bool CheckCriteria(
		const Api::SearchDiagnostic &diagnostic,
		Api::SearchCriteria criteria,
		const char *message) {
	return Check(
		diagnostic.hasQuery == criteria.hasQuery
			&& diagnostic.hasFrom == criteria.hasFrom
			&& diagnostic.hasTags == criteria.hasTags
			&& diagnostic.hasTopic == criteria.hasTopic
			&& diagnostic.hasFilter == criteria.hasFilter,
		message);
}

[[nodiscard]] bool CheckFilters() {
	using Filter = Api::SearchFilter;
	const auto expected = std::array{
		std::pair(Filter::NoFilter, mtpc_inputMessagesFilterEmpty),
		std::pair(Filter::Photos, mtpc_inputMessagesFilterPhotos),
		std::pair(Filter::Videos, mtpc_inputMessagesFilterVideo),
		std::pair(Filter::Files, mtpc_inputMessagesFilterDocument),
		std::pair(Filter::Links, mtpc_inputMessagesFilterUrl),
		std::pair(Filter::Music, mtpc_inputMessagesFilterMusic),
		std::pair(Filter::VoiceMessages, mtpc_inputMessagesFilterVoice),
		std::pair(Filter::VideoMessages, mtpc_inputMessagesFilterRoundVideo),
		std::pair(Filter::Gifs, mtpc_inputMessagesFilterGif),
		std::pair(Filter::Polls, mtpc_inputMessagesFilterPoll),
		std::pair(Filter::MyMentions, mtpc_inputMessagesFilterMyMentions),
		std::pair(Filter::Locations, mtpc_inputMessagesFilterGeo),
		std::pair(Filter::Pinned, mtpc_inputMessagesFilterPinned),
	};
	auto good = Check(
		Api::SearchFilters().size() == expected.size(),
		"filter menu size");
	for (auto i = size_t(0); i != expected.size(); ++i) {
		const auto &[filter, constructor] = expected[i];
		good &= Check(
			Api::SearchFilters()[i] == filter,
			"filter menu order");
		good &= Check(
			Api::PrepareSearchFilter(filter).type() == constructor,
			"filter constructor mapping");
	}
	const auto photos = Api::MessagesSearch::Request{
		.query = u"same"_q,
		.filter = Filter::Photos,
	};
	const auto videos = Api::MessagesSearch::Request{
		.query = u"same"_q,
		.filter = Filter::Videos,
	};
	good &= Check(photos != videos, "filter request identity");
	return good;
}

[[nodiscard]] bool CheckSearchSelectionPolicy() {
	using Change = Api::SearchSelectionChange;
	using Filter = Api::SearchFilter;
	struct Case {
		Change change = Change::Sender;
		bool senderSelected = false;
		Filter filter = Filter::NoFilter;
		bool exactIntersection = false;
		bool clearSender = false;
		bool clearFilter = false;
		const char *name = nullptr;
	};
	const auto cases = std::array{
		Case{ Change::Sender, true, Filter::Photos, false,
			false, true, "sender off clears filter" },
		Case{ Change::Filter, true, Filter::Photos, false,
			true, false, "filter off clears sender" },
		Case{ Change::Sender, true, Filter::Photos, true,
			false, false, "sender on preserves intersection" },
		Case{ Change::Filter, true, Filter::Photos, true,
			false, false, "filter on preserves intersection" },
		Case{ Change::Filter, true, Filter::NoFilter, false,
			false, false, "all preserves sender" },
		Case{ Change::Sender, false, Filter::Photos, false,
			false, false, "sender removal preserves filter" },
		Case{ Change::Sender, true, Filter::NoFilter, false,
			false, false, "sender-only selection is unchanged" },
		Case{ Change::Filter, false, Filter::Videos, false,
			false, false, "filter-only selection is unchanged" },
		Case{ Change::Sender, true, Filter::Pinned, false,
			false, true, "sender off clears dynamic pinned" },
		Case{ Change::Filter, true, Filter::Pinned, false,
			true, false, "dynamic pinned off clears sender" },
	};
	auto good = true;
	for (const auto &test : cases) {
		const auto result = Api::NormalizeSearchSelection(
			test.change,
			test.senderSelected,
			test.filter,
			test.exactIntersection);
		good &= Check(
			result.clearSender == test.clearSender
				&& result.clearFilter == test.clearFilter,
			test.name);
		good &= Check(
			!result.clearSender || test.change == Change::Filter,
			"policy clears sender only for filter actions");
		good &= Check(
			!result.clearFilter || test.change == Change::Sender,
			"policy clears filter only for sender actions");
		good &= Check(
			!result.clearSender || !result.clearFilter,
			"policy clears at most one criterion");
	}
	return good;
}

[[nodiscard]] bool CheckIntersectionRoutePolicy() {
	using Filter = Api::SearchFilter;
	struct Case {
		bool enabled = false;
		bool fixedFilter = false;
		bool senderSelected = false;
		Filter filter = Filter::NoFilter;
		bool expected = false;
		const char *name = nullptr;
	};
	const auto cases = std::array{
		Case{ true, false, true, Filter::Photos, true,
			"enabled dynamic sender and filter uses intersection" },
		Case{ false, false, true, Filter::Photos, false,
			"disabled experiment uses ordinary search" },
		Case{ true, true, true, Filter::Pinned, false,
			"fixed pinned search stays ordinary" },
		Case{ true, false, false, Filter::Photos, false,
			"filter only stays ordinary" },
		Case{ true, false, true, Filter::NoFilter, false,
			"sender only stays ordinary" },
	};
	auto good = true;
	for (const auto &test : cases) {
		good &= Check(
			Api::ShouldUseSearchIntersection(
				test.enabled,
				test.fixedFilter,
				test.senderSelected,
				test.filter) == test.expected,
			test.name);
	}
	return good;
}

[[nodiscard]] bool CheckIntersectionRequestSplit() {
	auto *sender = reinterpret_cast<PeerData*>(uintptr_t(1));
	const auto request = Api::MessagesSearch::Request{
		.query = u"query"_q,
		.from = sender,
		.tags = { Data::ReactionId{ u"tag"_q } },
		.topMsgId = MsgId(44),
		.filter = Api::SearchFilter::Photos,
	};
	const auto split = Api::PrepareSearchIntersectionRequests(request);
	return Check(
		split.sender.query == request.query
			&& split.sender.from == sender
			&& split.sender.tags == request.tags
			&& split.sender.topMsgId == request.topMsgId
			&& split.sender.filter == Api::SearchFilter::NoFilter
			&& split.filter.query == request.query
			&& split.filter.from == nullptr
			&& split.filter.tags == request.tags
			&& split.filter.topMsgId == request.topMsgId
			&& split.filter.filter == Api::SearchFilter::Photos,
		"intersection requests preserve query tags topic and split criteria");
}

enum class DirectCompletion {
	Success,
	Empty,
	QueryEmpty,
	RpcFailure,
	Cancelled,
	Timeout,
};

struct DirectCase {
	DirectCompletion completion = DirectCompletion::Cancelled;
	Api::SearchOutcomeType expected = Api::SearchOutcomeType::Cancelled;
	const char *name = nullptr;
};

[[nodiscard]] std::optional<Api::SearchOutcome> CompleteDirect(
		Api::SearchOperationState &state,
		Api::SearchGeneration generation,
		Api::SearchPage page,
		DirectCompletion completion) {
	switch (completion) {
	case DirectCompletion::Success:
		return state.succeed(generation, MakeFound(4, { 11 }));
	case DirectCompletion::Empty:
		return state.succeed(
			generation,
			MakeFound(page == Api::SearchPage::First ? 9 : 7, {}));
	case DirectCompletion::QueryEmpty:
		return state.fail(generation, u"SEARCH_QUERY_EMPTY"_q, 400);
	case DirectCompletion::RpcFailure:
		return state.fail(generation, u"FLOOD_WAIT_12"_q, 420);
	case DirectCompletion::Cancelled:
		return state.cancel(generation);
	case DirectCompletion::Timeout:
		return state.timeout(generation);
	}
	return std::nullopt;
}

[[nodiscard]] bool CheckDirectStates() {
	const auto criteria = Api::SearchCriteria{
		.hasQuery = true,
		.hasTags = true,
		.hasFilter = true,
	};
	const auto cases = std::array{
		DirectCase{ DirectCompletion::Success,
			Api::SearchOutcomeType::Success, "direct nonempty success" },
		DirectCase{ DirectCompletion::Empty,
			Api::SearchOutcomeType::Empty, "direct ordinary empty" },
		DirectCase{ DirectCompletion::QueryEmpty,
			Api::SearchOutcomeType::RpcFailure, "direct query-empty failure" },
		DirectCase{ DirectCompletion::RpcFailure,
			Api::SearchOutcomeType::RpcFailure, "direct rpc failure" },
		DirectCase{ DirectCompletion::Cancelled,
			Api::SearchOutcomeType::Cancelled, "direct cancellation" },
		DirectCase{ DirectCompletion::Timeout,
			Api::SearchOutcomeType::Timeout, "direct timeout" },
	};
	const auto pages = std::array{
		Api::SearchPage::First,
		Api::SearchPage::More,
	};
	auto good = true;
	for (const auto page : pages) {
		for (const auto &test : cases) {
			auto state = Api::SearchOperationState();
			const auto generation = Api::AllocateSearchGeneration();
			good &= Check(
				!state.begin(generation, page, criteria),
				"direct begin has no replacement");
			good &= Check(state.pending(), "direct begins pending");
			const auto outcome = CompleteDirect(
				state,
				generation,
				page,
				test.completion);
			good &= Check(bool(outcome), test.name);
			if (!outcome) {
				continue;
			}
			good &= Check(outcome->type == test.expected, test.name);
			good &= Check(
				outcome->generation == generation && outcome->page == page,
				"direct outcome identity");
			good &= CheckCriteria(
				outcome->diagnostic,
				criteria,
				"direct criterion snapshot");
			good &= Check(!state.pending(), "direct terminal closes pending");
			if (test.completion == DirectCompletion::Success) {
				good &= Check(
					outcome->found.messages == MakeIds({ 11 }),
					"direct success payload");
			} else if (test.completion == DirectCompletion::Empty) {
				good &= Check(
					outcome->found.total
						== (page == Api::SearchPage::First ? 0 : 7),
					"direct ordinary empty total");
			} else if (test.completion == DirectCompletion::QueryEmpty) {
				good &= Check(
					outcome->diagnostic.rpcType == u"SEARCH_QUERY_EMPTY"_q
						&& outcome->diagnostic.rpcCode == 400,
					"query-empty preserves rpc metadata");
			} else if (test.completion == DirectCompletion::RpcFailure) {
				good &= Check(
					outcome->diagnostic.rpcType == u"FLOOD_WAIT_12"_q
						&& outcome->diagnostic.rpcCode == 420,
					"ordinary rpc metadata");
			}
			good &= Check(
				!CompleteDirect(
					state,
					generation,
					page,
					test.completion),
				"direct duplicate terminal rejected");
			good &= Check(
				!state.succeed(generation, MakeFound(1, { 12 }))
					&& !state.fail(generation, u"RPC_ERROR"_q, 500)
					&& !state.cancel(generation)
					&& !state.timeout(generation),
				"direct alternate terminals rejected");
		}
	}
	return good;
}

[[nodiscard]] bool CheckDirectReplacement() {
	auto state = Api::SearchOperationState();
	const auto oldGeneration = Api::AllocateSearchGeneration();
	const auto newGeneration = Api::AllocateSearchGeneration();
	const auto criteria = Api::SearchCriteria{ .hasQuery = true };
	auto good = Check(
		!state.begin(oldGeneration, Api::SearchPage::First, criteria),
		"replacement old begin");
	const auto action = state.begin(
		newGeneration,
		Api::SearchPage::First,
		criteria);
	auto logicalCancels = 0;
	if (action && action->type == Api::SearchOutcomeType::Cancelled) {
		++logicalCancels;
	}
	good &= Check(
		action && action->generation == oldGeneration,
		"replacement returns old cancellation");
	good &= Check(logicalCancels == 1, "replacement cancel executes once");
	good &= Check(
		!state.succeed(oldGeneration, MakeFound(1, { 31 }))
			&& !state.fail(oldGeneration, u"LATE_FAILURE"_q, 500)
			&& !state.cancel(oldGeneration),
		"replacement rejects late done fail cancel");
	good &= Check(
		state.pending() && state.isCurrent(newGeneration),
		"replacement leaves new generation pending");
	const auto completed = state.succeed(
		newGeneration,
		MakeFound(1, { 32 }));
	good &= Check(
		completed && completed->type == Api::SearchOutcomeType::Success,
		"replacement new generation completes");
	good &= Check(
		!state.succeed(newGeneration, MakeFound(1, { 33 })),
		"replacement new generation completes once");
	good &= Check(logicalCancels == 1, "replacement needs no fail callback");
	return good;
}

struct CancelCounts {
	int active = 0;
	int migrated = 0;
};

void ExecuteCancels(Api::SearchCancelMask mask, CancelCounts &counts) {
	if (mask.active) {
		++counts.active;
	}
	if (mask.migrated) {
		++counts.migrated;
	}
}

[[nodiscard]] Api::SearchOutcome ChildFound(
		Api::SearchGeneration generation,
		bool nonempty,
		Api::FoundMessages found) {
	return nonempty
		? Api::SearchOutcome::Success(
			generation,
			Api::SearchPage::First,
			{},
			std::move(found))
		: Api::SearchOutcome::Empty(
			generation,
			Api::SearchPage::First,
			{},
			std::move(found));
}

[[nodiscard]] bool CheckMergedSuccessfulFirstPages() {
	struct Case {
		bool active = false;
		bool migrated = false;
		Api::SearchOutcomeType expected = Api::SearchOutcomeType::Empty;
	};
	const auto cases = std::array{
		Case{ true, true, Api::SearchOutcomeType::Success },
		Case{ true, false, Api::SearchOutcomeType::Success },
		Case{ false, false, Api::SearchOutcomeType::Empty },
	};
	auto good = true;
	for (const auto &test : cases) {
		for (const auto migratedFirst : { false, true }) {
			auto state = Api::SearchMergedState();
			const auto generation = Api::AllocateSearchGeneration();
			const auto activeGeneration = Api::AllocateSearchGeneration();
			const auto migratedGeneration = Api::AllocateSearchGeneration();
			good &= Check(
				!state.begin(
					generation,
					Api::SearchPage::First,
					{},
					activeGeneration,
					migratedGeneration).outcome,
				"merged first begins without replacement");
			const auto active = ChildFound(
				activeGeneration,
				test.active,
				test.active ? MakeFound(2, { 11, 12 }) : MakeFound(0, {}));
			const auto migrated = ChildFound(
				migratedGeneration,
				test.migrated,
				test.migrated ? MakeFound(2, { 21, 22 }) : MakeFound(0, {}));
			const auto first = migratedFirst
				? state.accept(Api::SearchBranch::Migrated, migrated)
				: state.accept(Api::SearchBranch::Active, active);
			good &= Check(
				!first.outcome && !first.cancel.active && !first.cancel.migrated,
				"merged waits for successful sibling");
			const auto second = migratedFirst
				? state.accept(Api::SearchBranch::Active, active)
				: state.accept(Api::SearchBranch::Migrated, migrated);
			good &= Check(
				second.outcome && second.outcome->type == test.expected,
				"merged successful terminal type");
			if (!second.outcome) {
				continue;
			}
			good &= Check(
				second.outcome->found.total
					== (test.active ? 2 : 0) + (test.migrated ? 2 : 0),
				"merged sums totals");
			auto expected = test.active ? MakeIds({ 11, 12 }) : MessageIdsList();
			if (test.migrated) {
				const auto more = MakeIds({ 21, 22 });
				expected.insert(end(expected), begin(more), end(more));
			}
			good &= Check(
				second.outcome->found.messages == expected,
				"merged active-before-migrated order");
			good &= Check(!state.pending(), "merged success closes pending");
			good &= Check(
				!state.accept(Api::SearchBranch::Active, active).outcome,
				"merged successful duplicate rejected");
		}
	}
	return good;
}

[[nodiscard]] bool CheckMergedFailure() {
	auto good = true;
	for (const auto failed : {
			Api::SearchBranch::Active,
			Api::SearchBranch::Migrated }) {
		auto state = Api::SearchMergedState();
		const auto generation = Api::AllocateSearchGeneration();
		const auto activeGeneration = Api::AllocateSearchGeneration();
		const auto migratedGeneration = Api::AllocateSearchGeneration();
		good &= Check(
			!state.begin(
				generation,
				Api::SearchPage::First,
				{},
				activeGeneration,
				migratedGeneration).outcome,
			"merged failure begins pending");
		const auto childGeneration = (failed == Api::SearchBranch::Active)
			? activeGeneration
			: migratedGeneration;
		const auto failure = Api::SearchOutcome::RpcFailure(
			childGeneration,
			Api::SearchPage::First,
			{},
			u"SEARCH_FAILED"_q,
			500);
		const auto terminal = state.accept(failed, failure);
		good &= Check(
			terminal.outcome
				&& terminal.outcome->type == Api::SearchOutcomeType::RpcFailure,
			"merged propagates one-leg failure");
		good &= Check(
			terminal.cancel.active == (failed == Api::SearchBranch::Migrated)
				&& terminal.cancel.migrated
					== (failed == Api::SearchBranch::Active),
			"merged cancels pending sibling");
		auto counts = CancelCounts();
		ExecuteCancels(terminal.cancel, counts);
		good &= Check(
			counts.active + counts.migrated == 1,
			"merged sibling cancel executes once");
		const auto sibling = (failed == Api::SearchBranch::Active)
			? Api::SearchBranch::Migrated
			: Api::SearchBranch::Active;
		const auto siblingGeneration = (failed == Api::SearchBranch::Active)
			? migratedGeneration
			: activeGeneration;
		good &= Check(
			!state.accept(
				sibling,
				Api::SearchOutcome::Cancelled(
					siblingGeneration,
					Api::SearchPage::First,
					{})).outcome,
			"merged rejects sibling cancellation after terminal");
		good &= Check(!state.pending(), "merged failure closes pending");
	}
	return good;
}

[[nodiscard]] bool CheckMergedCancellationAndReplacement() {
	auto good = true;
	{
		auto state = Api::SearchMergedState();
		const auto generation = Api::AllocateSearchGeneration();
		good &= Check(
			!state.begin(
				generation,
				Api::SearchPage::First,
				{},
				Api::AllocateSearchGeneration(),
				Api::AllocateSearchGeneration()).outcome,
			"merged clear begins pending");
		const auto cleared = state.cancel(generation);
		good &= Check(
			cleared.outcome
				&& cleared.outcome->type == Api::SearchOutcomeType::Cancelled
				&& cleared.cancel.active
				&& cleared.cancel.migrated,
			"merged clear cancels operation and branches");
		auto counts = CancelCounts();
		ExecuteCancels(cleared.cancel, counts);
		ExecuteCancels(state.cancel(generation).cancel, counts);
		good &= Check(
			counts.active == 1 && counts.migrated == 1,
			"merged clear cancellation executes once");
	}
	{
		auto state = Api::SearchMergedState();
		const auto oldGeneration = Api::AllocateSearchGeneration();
		const auto oldActive = Api::AllocateSearchGeneration();
		good &= Check(
			!state.begin(
				oldGeneration,
				Api::SearchPage::First,
				{},
				oldActive,
				Api::AllocateSearchGeneration()).outcome,
			"merged replacement old begin");
		const auto newGeneration = Api::AllocateSearchGeneration();
		const auto newActive = Api::AllocateSearchGeneration();
		const auto replaced = state.begin(
			newGeneration,
			Api::SearchPage::First,
			{},
			newActive,
			0);
		good &= Check(
			replaced.outcome
				&& replaced.outcome->generation == oldGeneration
				&& replaced.cancel.active
				&& replaced.cancel.migrated,
			"merged replacement returns old cancellation");
		auto counts = CancelCounts();
		ExecuteCancels(replaced.cancel, counts);
		good &= Check(
			counts.active == 1 && counts.migrated == 1,
			"merged replacement cancels old children once");
		good &= Check(
			!state.accept(
				Api::SearchBranch::Active,
				Api::SearchOutcome::Success(
					oldActive,
					Api::SearchPage::First,
					{},
					MakeFound(1, { 41 }))).outcome,
			"merged replacement rejects stale child");
		good &= Check(
			state.pending() && state.isCurrent(newGeneration),
			"merged replacement keeps new generation pending");
		good &= Check(
			state.accept(
				Api::SearchBranch::Active,
				Api::SearchOutcome::Success(
					newActive,
					Api::SearchPage::First,
					{},
					MakeFound(1, { 42 }))).outcome.has_value(),
			"merged replacement new generation completes");
	}
	return good;
}

[[nodiscard]] bool CheckMergedWatchdog() {
	auto state = Api::SearchMergedState();
	const auto generation = Api::AllocateSearchGeneration();
	const auto activeGeneration = Api::AllocateSearchGeneration();
	const auto migratedGeneration = Api::AllocateSearchGeneration();
	auto good = Check(
		!state.begin(
			generation,
			Api::SearchPage::First,
			{},
			activeGeneration,
			migratedGeneration).outcome,
		"merged watchdog begins pending");
	good &= Check(
		!state.accept(
			Api::SearchBranch::Active,
			Api::SearchOutcome::Success(
				Api::AllocateSearchGeneration(),
				Api::SearchPage::First,
				{},
				MakeFound(1, { 51 }))).outcome
			&& state.pending(),
		"merged rejects stale child id");
	const auto expired = state.timeout(generation);
	good &= Check(
		expired.outcome
			&& expired.outcome->type == Api::SearchOutcomeType::Timeout
			&& expired.cancel.active
			&& expired.cancel.migrated,
		"merged watchdog cancels all pending branches");
	auto counts = CancelCounts();
	ExecuteCancels(expired.cancel, counts);
	ExecuteCancels(state.timeout(generation).cancel, counts);
	good &= Check(
		counts.active == 1 && counts.migrated == 1,
		"merged watchdog cancels each branch once");
	good &= Check(
		!state.accept(
			Api::SearchBranch::Migrated,
			Api::SearchOutcome::Cancelled(
				migratedGeneration,
				Api::SearchPage::First,
				{})).outcome,
		"merged watchdog rejects late cancellation");
	good &= Check(!state.pending(), "merged watchdog closes pending");
	return good;
}

[[nodiscard]] Api::SearchCombinedMessages PaginationBase(
		Api::SearchBranch branch) {
	return (branch == Api::SearchBranch::Active)
		? Api::CombineSearchFirstPage(
			MakeFound(2, { 101 }, u"active-first"_q),
			MakeFound(2, { 201, 202 }, u"migrated-first"_q))
		: Api::CombineSearchFirstPage(
			MakeFound(1, { 101 }, u"active-first"_q),
			MakeFound(2, { 201 }, u"migrated-first"_q));
}

[[nodiscard]] Api::SearchOutcome PaginationOutcome(
		Api::SearchOutcomeType type,
		Api::SearchGeneration generation,
		Api::SearchBranch branch) {
	const auto message = (branch == Api::SearchBranch::Active) ? 102 : 202;
	switch (type) {
	case Api::SearchOutcomeType::Success:
		return Api::SearchOutcome::Success(
			generation,
			Api::SearchPage::More,
			{},
			MakeFound(2, { message }, u"page"_q));
	case Api::SearchOutcomeType::Empty:
		return Api::SearchOutcome::Empty(
			generation,
			Api::SearchPage::More,
			{},
			MakeFound(2, {}));
	case Api::SearchOutcomeType::RpcFailure:
		return Api::SearchOutcome::RpcFailure(
			generation,
			Api::SearchPage::More,
			{},
			u"PAGE_FAILED"_q,
			500);
	case Api::SearchOutcomeType::Cancelled:
		return Api::SearchOutcome::Cancelled(
			generation,
			Api::SearchPage::More,
			{});
	case Api::SearchOutcomeType::Timeout:
		return Api::SearchOutcome::Timeout(
			generation,
			Api::SearchPage::More,
			{});
	}
	return Api::SearchOutcome::Cancelled(
		generation,
		Api::SearchPage::More,
		{});
}

[[nodiscard]] Api::SearchOutcome IntersectionFound(
		Api::SearchIntersectionRequest request,
		MessageIdsList messages) {
	return Api::SearchOutcome::FromFound(
		request.generation,
		request.first ? Api::SearchPage::First : Api::SearchPage::More,
		{},
		Api::FoundMessages{
			.total = int(messages.size()),
			.messages = std::move(messages),
		});
}

[[nodiscard]] bool CheckIntersectionOrderingAndMigration() {
	auto state = Api::SearchIntersectionState({
		.pageSize = 10,
		.maxPagesPerLeg = 3,
	});
	const auto active = PeerId(UserId(1));
	const auto migrated = PeerId(UserId(2));
	const auto generation = Api::AllocateSearchGeneration();
	auto action = state.begin(generation, {}, active, migrated);
	auto sender = MakePeerIds(1, { 7, 9, 7, 5 });
	const auto senderMigrated = MakePeerIds(2, { 100, 80 });
	sender.insert(end(sender), begin(senderMigrated), end(senderMigrated));
	auto filter = MakePeerIds(1, { 10, 9, 7, 6, 5 });
	const auto filterMigrated = MakePeerIds(2, { 100, 90, 80 });
	filter.insert(end(filter), begin(filterMigrated), end(filterMigrated));
	auto good = Check(
		action.senderRequest.first && action.filterRequest.first,
		"intersection starts two first-page legs");
	auto next = state.accept(
		Api::SearchIntersectionLeg::Sender,
		IntersectionFound(action.senderRequest, std::move(sender)),
		true);
	good &= Check(!next.outcome, "intersection waits for second leg");
	next = state.accept(
		Api::SearchIntersectionLeg::Filter,
		IntersectionFound(action.filterRequest, std::move(filter)),
		true);
	auto expected = MakePeerIds(1, { 9, 7, 5 });
	const auto expectedMigrated = MakePeerIds(2, { 100, 80 });
	expected.insert(
		end(expected),
		begin(expectedMigrated),
		end(expectedMigrated));
	good &= Check(
		next.outcome
			&& next.outcome->type == Api::SearchOutcomeType::Success
			&& next.outcome->found.messages == expected
			&& next.outcome->found.total == int(expected.size())
			&& !next.outcome->found.hasMore
			&& !next.outcome->found.partial,
		"intersection is active-first descending and deduplicated");
	good &= Check(
		state.messages().messages == expected
			&& state.messages().total == int(expected.size()),
		"intersection complete aggregate matches outcome");
	return good;
}

[[nodiscard]] bool CheckIntersectionSparsePagination() {
	auto state = Api::SearchIntersectionState({
		.pageSize = 2,
		.maxPagesPerLeg = 5,
	});
	const auto generation = Api::AllocateSearchGeneration();
	auto action = state.begin(
		generation,
		Api::SearchCriteria{ .hasQuery = true, .hasTopic = true },
		PeerId(UserId(1)));
	auto good = true;
	auto next = state.accept(
		Api::SearchIntersectionLeg::Sender,
		IntersectionFound(
			action.senderRequest,
			MakeIds({ 100, 90 })),
		false);
	good &= Check(!next.outcome, "sparse sender waits for filter");
	next = state.accept(
		Api::SearchIntersectionLeg::Filter,
		IntersectionFound(action.filterRequest, MakeIds({ 99 })),
		false);
	good &= Check(
		!next.outcome
			&& next.filterRequest.generation
			&& !next.senderRequest.generation,
		"sparse merge advances only depleted filter leg");
	next = state.accept(
		Api::SearchIntersectionLeg::Filter,
		IntersectionFound(next.filterRequest, MakeIds({ 90, 80 })),
		false);
	good &= Check(
		!next.outcome
			&& next.senderRequest.generation
			&& !next.filterRequest.generation,
		"sparse merge retains filter buffer while paging sender");
	next = state.accept(
		Api::SearchIntersectionLeg::Sender,
		IntersectionFound(next.senderRequest, MakeIds({ 80, 70 })),
		true);
	good &= Check(
		next.outcome
			&& next.outcome->found.messages == MakeIds({ 90, 80 })
			&& next.outcome->found.total == -1
			&& next.outcome->found.hasMore
			&& !next.outcome->found.partial
			&& state.canSearchMore(),
		"sparse first intersection page keeps total unknown");
	const auto moreGeneration = Api::AllocateSearchGeneration();
	next = state.more(moreGeneration);
	good &= Check(
		!next.outcome && next.filterRequest.generation,
		"intersection more resumes retained sender buffer");
	next = state.accept(
		Api::SearchIntersectionLeg::Filter,
		IntersectionFound(next.filterRequest, MakeIds({ 70 })),
		true);
	good &= Check(
		next.outcome
			&& next.outcome->page == Api::SearchPage::More
			&& next.outcome->found.messages == MakeIds({ 70 })
			&& next.outcome->found.total == 3
			&& !next.outcome->found.hasMore
			&& state.messages().messages == MakeIds({ 90, 80, 70 })
			&& state.messages().total == 3
			&& !state.canSearchMore(),
		"intersection more proves total and commits exact delta");
	return good;
}

[[nodiscard]] bool CheckIntersectionBoundsAndFailures() {
	auto good = true;
	{
		auto state = Api::SearchIntersectionState({
			.pageSize = 50,
			.maxPagesPerLeg = 1,
		});
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			{},
			PeerId(UserId(1)));
		auto next = state.accept(
			Api::SearchIntersectionLeg::Sender,
			IntersectionFound(action.senderRequest, MakeIds({ 10 })),
			false);
		next = state.accept(
			Api::SearchIntersectionLeg::Filter,
			IntersectionFound(action.filterRequest, MakeIds({ 9 })),
			false);
		good &= Check(
			next.outcome
				&& next.outcome->type == Api::SearchOutcomeType::Empty
				&& next.outcome->found.total == -1
				&& !next.outcome->found.hasMore
				&& next.outcome->found.partial
				&& !state.canSearchMore()
				&& !state.more(Api::AllocateSearchGeneration()).outcome,
			"intersection cap is terminal partial unknown");
	}
	{
		auto state = Api::SearchIntersectionState({
			.pageSize = 50,
			.maxPagesPerLeg = 3,
		});
		const auto criteria = Api::SearchCriteria{
			.hasQuery = true,
			.hasFrom = true,
			.hasTopic = true,
			.hasFilter = true,
		};
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			criteria,
			PeerId(UserId(1)));
		auto next = state.accept(
			Api::SearchIntersectionLeg::Sender,
			IntersectionFound(action.senderRequest, MakeIds({ 10 })),
			false);
		next = state.accept(
			Api::SearchIntersectionLeg::Filter,
			IntersectionFound(action.filterRequest, MakeIds({ 10 })),
			false);
		const auto pendingFilter = next.filterRequest;
		next = state.timeout(state.generation());
		good &= Check(
			next.outcome
				&& next.outcome->type == Api::SearchOutcomeType::Timeout
				&& next.outcome->found.messages == MakeIds({ 10 })
				&& next.outcome->found.partial
				&& next.cancelSender
				&& next.cancelFilter
				&& state.messages().messages == MakeIds({ 10 })
				&& state.messages().total == -1
				&& !state.messages().hasMore
				&& state.messages().partial,
			"intersection timeout preserves partial and cancels legs");
		good &= Check(
			!state.accept(
				Api::SearchIntersectionLeg::Filter,
				IntersectionFound(pendingFilter, MakeIds({ 8 })),
				true).outcome,
			"intersection rejects stale result after timeout");
	}
	{
		auto state = Api::SearchIntersectionState();
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			Api::SearchCriteria{ .hasFrom = true, .hasFilter = true },
			PeerId(UserId(1)));
		const auto failure = Api::SearchOutcome::RpcFailure(
			action.senderRequest.generation,
			Api::SearchPage::First,
			{},
			u"SEARCH_FAILED"_q,
			500);
		const auto next = state.accept(
			Api::SearchIntersectionLeg::Sender,
			failure,
			false);
		good &= Check(
			next.outcome
				&& next.outcome->type == Api::SearchOutcomeType::RpcFailure
				&& next.outcome->diagnostic.rpcType == u"SEARCH_FAILED"_q
				&& next.outcome->diagnostic.hasFrom
				&& next.outcome->diagnostic.hasFilter
				&& next.outcome->found.partial
				&& next.cancelFilter,
			"intersection propagates sanitized failure and cancels sibling");
	}
	return good;
}

[[nodiscard]] bool CheckIntersectionCancellationAndInput() {
	auto state = Api::SearchIntersectionState();
	const auto generation = Api::AllocateSearchGeneration();
	auto action = state.begin(
		generation,
		{},
		PeerId(UserId(1)),
		PeerId(UserId(2)));
	const auto staleSender = action.senderRequest;
	const auto replacementGeneration = Api::AllocateSearchGeneration();
	auto replaced = state.begin(
		replacementGeneration,
		{},
		PeerId(UserId(1)),
		PeerId(UserId(2)));
	auto good = Check(
		replaced.outcome
			&& replaced.outcome->generation == generation
			&& replaced.outcome->type == Api::SearchOutcomeType::Cancelled
			&& replaced.cancelSender
			&& replaced.cancelFilter
			&& replaced.senderRequest.generation
			&& replaced.filterRequest.generation
			&& state.pending()
			&& state.generation() == replacementGeneration,
		"intersection replacement cancels stale legs and starts new legs");
	good &= Check(
		!state.accept(
			Api::SearchIntersectionLeg::Sender,
			IntersectionFound(staleSender, MakeIds({ 4 })),
			true).outcome,
		"intersection replacement rejects stale child");
	const auto replacementSender = replaced.senderRequest;
	const auto replacementFilter = replaced.filterRequest;
	auto cancelled = state.cancel(replacementGeneration);
	good &= Check(
		cancelled.outcome
			&& cancelled.outcome->type == Api::SearchOutcomeType::Cancelled
			&& cancelled.cancelSender
			&& cancelled.cancelFilter,
		"intersection cancellation terminates both legs");
	good &= Check(
		!state.accept(
			Api::SearchIntersectionLeg::Sender,
			IntersectionFound(replacementSender, MakeIds({ 4 })),
			true).outcome
			&& !state.accept(
				Api::SearchIntersectionLeg::Filter,
				IntersectionFound(replacementFilter, MakeIds({ 4 })),
				true).outcome,
		"intersection cancellation rejects stale children");
	action = state.begin(
		Api::AllocateSearchGeneration(),
		{},
		PeerId(UserId(1)),
		PeerId(UserId(2)));
	const auto invalid = state.accept(
		Api::SearchIntersectionLeg::Sender,
		IntersectionFound(
			action.senderRequest,
			MakePeerIds(3, { 4 })),
		true);
	good &= Check(
		invalid.outcome
			&& invalid.outcome->type == Api::SearchOutcomeType::RpcFailure
			&& invalid.outcome->diagnostic.rpcType
				== u"SEARCH_ORDER_INVALID"_q
			&& invalid.cancelFilter,
		"intersection rejects messages outside active migration pair");
	return good;
}

[[nodiscard]] bool CheckIntersectionCumulativeExhaustion() {
	const auto delta = Api::FoundMessages{
		.total = 4,
		.messages = MakeIds({ 40 }),
	};
	const auto cumulative = Api::FoundMessages{
		.total = 4,
		.messages = MakeIds({ 70, 60, 50, 40 }),
	};
	return Check(
		!Api::SearchIntersectionExhausted(delta)
			&& Api::SearchIntersectionExhausted(cumulative),
		"intersection exhaustion uses merged cumulative not next delta");
}

[[nodiscard]] bool CheckPagination() {
	const auto types = std::array{
		Api::SearchOutcomeType::Success,
		Api::SearchOutcomeType::Empty,
		Api::SearchOutcomeType::RpcFailure,
		Api::SearchOutcomeType::Cancelled,
		Api::SearchOutcomeType::Timeout,
	};
	auto good = true;
	for (const auto branch : {
			Api::SearchBranch::Active,
			Api::SearchBranch::Migrated }) {
		for (const auto type : types) {
			const auto baseline = PaginationBase(branch);
			const auto committed = baseline.committed;
			auto state = Api::SearchMergedState();
			const auto generation = Api::AllocateSearchGeneration();
			const auto childGeneration = Api::AllocateSearchGeneration();
			good &= Check(
				!state.begin(
					generation,
					Api::SearchPage::More,
					{},
					(branch == Api::SearchBranch::Active)
						? childGeneration
						: 0,
					(branch == Api::SearchBranch::Migrated)
						? childGeneration
						: 0).outcome,
				"pagination begins pending");
			const auto child = PaginationOutcome(type, childGeneration, branch);
			const auto terminal = state.accept(branch, child);
			good &= Check(
				terminal.outcome && terminal.outcome->type == type,
				"pagination propagates terminal type");
			good &= Check(!state.pending(), "pagination closes pending");
			good &= Check(
				!state.accept(branch, child).outcome,
				"pagination duplicate callback rejected");
			if (type == Api::SearchOutcomeType::Success
				|| type == Api::SearchOutcomeType::Empty) {
				const auto combined = Api::CombineSearchPage(
					baseline,
					branch,
					child.found);
				good &= Check(
					combined.combined.next == Api::SearchNextBranch::None,
					"pagination final page closes retryable branch");
				if (branch == Api::SearchBranch::Active) {
					const auto expected = (type == Api::SearchOutcomeType::Success)
						? MakeIds({ 102, 201, 202 })
						: MakeIds({ 201, 202 });
					good &= Check(
						combined.delta.messages == expected,
						"active exhaustion exposes held migrated delta");
				} else {
					const auto expected = (type == Api::SearchOutcomeType::Success)
						? MakeIds({ 202 })
						: MessageIdsList();
					good &= Check(
						combined.delta.messages == expected,
						"migrated page delta");
				}
			} else {
				good &= Check(
					baseline.committed.total == committed.total
						&& baseline.committed.messages == committed.messages
						&& baseline.committed.nextToken == committed.nextToken,
					"pagination failure preserves committed aggregate");
			}
		}
	}
	return good;
}

[[nodiscard]] bool CheckDiagnostics() {
	using Formatter = decltype(&Api::FormatSearchDiagnostic);
	static_assert(std::is_same_v<
		Formatter,
		QString (*)(const Api::SearchDiagnostic&)>);
	static_assert(!std::is_invocable_v<Formatter, const Api::FoundMessages&>);
	static_assert(!std::is_invocable_v<Formatter, const Api::SearchOutcome&>);
	static_assert(!std::is_invocable_v<
		Formatter,
		const Api::MessagesSearch::Request&>);

	const auto valid = Api::SearchDiagnostic{
		.rpcType = u"FLOOD_WAIT_12"_q,
		.rpcCode = 420,
		.hasQuery = true,
		.hasTags = true,
		.hasFilter = true,
	};
	auto good = Check(
		Api::FormatSearchDiagnostic(valid)
			== u"rpcType=FLOOD_WAIT_12 rpcCode=420 hasQuery=yes "
				u"hasFrom=no hasTags=yes hasTopic=no hasFilter=yes"_q,
		"diagnostic exact seven-key valid output");
	const auto sentinels = std::array{
		u"QUERY-SENTINEL-8E31"_q,
		u"TOKEN-SENTINEL-93C2"_q,
		u"PEER-SENTINEL-40D1"_q,
		u"SENDER-SENTINEL-773A"_q,
		u"TOPIC-SENTINEL-5BE4"_q,
		u"MESSAGE-SENTINEL-012F"_q,
		u"ACCESS-HASH-SENTINEL-61A9"_q,
		u"REACTION-SENTINEL-20CC"_q,
		u"SESSION-SENTINEL-F8D4"_q,
		u"AUTH-SENTINEL-B635"_q,
		u"API-ID-SENTINEL-A241"_q,
		u"API-HASH-SENTINEL-EC10"_q,
		u"PHONE-SENTINEL-D907"_q,
		u"RAW-OBJECT-SENTINEL-3AC8"_q,
		u"ERROR-DESCRIPTION-SENTINEL-7B55"_q,
	};
	auto maliciousType = QString();
	for (const auto &sentinel : sentinels) {
		maliciousType += sentinel + u" "_q;
	}
	const auto malicious = Api::SearchDiagnostic{
		.rpcType = maliciousType,
		.rpcCode = -999,
		.hasFrom = true,
		.hasTopic = true,
	};
	const auto formatted = Api::FormatSearchDiagnostic(malicious);
	good &= Check(
		formatted
			== u"rpcType=RPC_ERROR rpcCode=-999 hasQuery=no "
				u"hasFrom=yes hasTags=no hasTopic=yes hasFilter=no"_q,
		"diagnostic malicious type fixed fallback");
	for (const auto &sentinel : sentinels) {
		good &= Check(
			!formatted.contains(sentinel),
			"diagnostic excludes forbidden sentinel");
	}
	return good;
}

[[nodiscard]] Api::SearchOutcome AdaptiveFound(
		Api::SearchIntersectionRequest request,
		int total,
		MessageIdsList messages) {
	return Api::SearchOutcome::FromFound(
		request.generation,
		request.first ? Api::SearchPage::First : Api::SearchPage::More,
		{},
		Api::FoundMessages{
			.total = total,
			.messages = std::move(messages),
		});
}

[[nodiscard]] bool CheckLocalFilterMatching() {
	using Filter = Api::SearchFilter;
	using Flag = Api::LocalSearchMessageFlag;
	const auto bit = [](Flag flag) {
		return static_cast<Api::LocalSearchMessageFlags>(flag);
	};
	const auto cases = std::array{
		std::pair(Filter::Photos, Flag::Photo),
		std::pair(Filter::Videos, Flag::Video),
		std::pair(Filter::Files, Flag::File),
		std::pair(Filter::Links, Flag::Link),
		std::pair(Filter::Music, Flag::Music),
		std::pair(Filter::VoiceMessages, Flag::VoiceMessage),
		std::pair(Filter::VideoMessages, Flag::VideoMessage),
		std::pair(Filter::Gifs, Flag::Gif),
		std::pair(Filter::Polls, Flag::Poll),
		std::pair(Filter::MyMentions, Flag::Mention),
		std::pair(Filter::Locations, Flag::Location),
		std::pair(Filter::Pinned, Flag::Pinned),
	};
	auto good = Check(
		Api::MatchesSearchFilterLocally(Filter::NoFilter, 0),
		"local no-filter always matches");
	for (const auto &[filter, flag] : cases) {
		good &= Check(
			Api::MatchesSearchFilterLocally(filter, bit(flag)),
			"local filter matches its message trait");
		good &= Check(
			!Api::MatchesSearchFilterLocally(filter, 0),
			"local filter rejects absent message trait");
	}
	good &= Check(
		Api::MatchesSearchSenderLocally(
			PeerId(UserId(42)),
			PeerId(UserId(42)))
			&& Api::MatchesSearchSenderLocally(
				PeerId(ChannelId(7)),
				PeerId(ChannelId(7)))
			&& !Api::MatchesSearchSenderLocally(
				PeerId(UserId(42)),
				PeerId(ChannelId(7))),
		"local sender matching supports user and channel identities");
	return good;
}

[[nodiscard]] bool CheckRawTraitsMatching() {
	using Filter = Api::SearchFilter;
	using Flag = Api::LocalSearchMessageFlag;
	const auto bit = [](Flag flag) {
		return static_cast<Api::LocalSearchMessageFlags>(flag);
	};
	const auto sender = PeerId(UserId(42));
	const auto other = PeerId(UserId(43));
	auto found = MakeFound(3, { 100, 90, 80 });
	found.traits.insert_or_assign(found.messages[0], Api::SearchMessageTraits{
		.sender = sender,
		.filterFlags = bit(Flag::Photo),
	});
	found.traits.insert_or_assign(found.messages[1], Api::SearchMessageTraits{
		.sender = other,
		.filterFlags = bit(Flag::Photo),
	});
	found.traits.insert_or_assign(found.messages[2], Api::SearchMessageTraits{
		.sender = sender,
		.filterFlags = bit(Flag::Video),
	});
	const auto matched = Api::SearchMessagesMatchingTraits(
		found,
		Filter::Photos,
		sender);
	auto good = Check(
		matched && *matched == MakeIds({ 100 }),
		"raw traits require both sender and message type");

	const auto laterPage = MakeFoundWithTraits(
		200,
		{ 70, 60 },
		sender,
		bit(Flag::Photo));
	const auto laterMatched = Api::SearchMessagesMatchingTraits(
		laterPage,
		Filter::Photos,
		sender);
	good &= Check(
		laterMatched && *laterMatched == MakeIds({ 70, 60 }),
		"raw photo traits keep sender-driver continuation results");

	auto missing = laterPage;
	missing.traits.erase(missing.messages.back());
	good &= Check(
		!Api::SearchMessagesMatchingTraits(
			missing,
			Filter::Photos,
			sender),
		"missing raw traits terminate local matching");

	const auto webpage = Api::SearchMessageTraits{
		.sender = sender,
		.filterFlags = bit(Flag::Link),
	};
	const auto excludedMedia = Api::SearchMessageTraits{
		.sender = sender,
		.filterFlags = 0,
	};
	good &= Check(
		Api::MatchesSearchTraitsLocally(webpage, Filter::Links, sender)
			&& !Api::MatchesSearchTraitsLocally(
				webpage,
				Filter::Photos,
				sender)
			&& !Api::MatchesSearchTraitsLocally(
				excludedMedia,
				Filter::Photos,
				sender)
			&& !Api::MatchesSearchTraitsLocally(
				excludedMedia,
				Filter::Files,
				sender),
		"webpage images are links and excluded photo document traits stay empty");

	auto identities = Api::FoundMessages{
		.total = 2,
		.messages = {
			FullMsgId(PeerId(ChannelId(7)), MsgId(11)),
			FullMsgId(PeerId(ChatId(8)), MsgId(10)),
		},
	};
	const auto channelIdentity = PeerId(ChannelId(77));
	for (const auto id : identities.messages) {
		identities.traits.insert_or_assign(id, Api::SearchMessageTraits{
			.sender = channelIdentity,
			.filterFlags = bit(Flag::Pinned),
		});
	}
	const auto identityMatches = Api::SearchMessagesMatchingTraits(
		identities,
		Filter::Pinned,
		channelIdentity);
	good &= Check(
		identityMatches && *identityMatches == identities.messages,
		"raw sender traits preserve channel identity across migrated peers");
	return good;
}

[[nodiscard]] bool CheckSearchTraitsMerge() {
	using Flag = Api::LocalSearchMessageFlag;
	const auto bit = [](Flag flag) {
		return static_cast<Api::LocalSearchMessageFlags>(flag);
	};
	const auto senderA = PeerId(UserId(10));
	const auto senderB = PeerId(ChannelId(20));
	const auto senderC = PeerId(UserId(30));
	auto good = true;
	{
		const auto combined = Api::CombineSearchFirstPage(
			MakeFoundWithTraits(
				1,
				{ 101 },
				senderA,
				bit(Flag::Photo)),
			MakeFoundWithTraits(
				1,
				{ 201 },
				senderB,
				bit(Flag::Video)));
		good &= Check(
			combined.committed.messages == MakeIds({ 101, 201 })
				&& HasTraits(
					combined.committed,
					MakeIds({ 101 })[0],
					{ senderA, bit(Flag::Photo) })
				&& HasTraits(
					combined.committed,
					MakeIds({ 201 })[0],
					{ senderB, bit(Flag::Video) }),
			"active and migrated first-page traits merge with ids");
	}
	{
		auto base = Api::CombineSearchFirstPage(
			MakeFoundWithTraits(
				2,
				{ 101 },
				senderA,
				bit(Flag::Photo)),
			MakeFoundWithTraits(
				1,
				{ 201 },
				senderB,
				bit(Flag::Video)));
		const auto page = Api::CombineSearchPage(
			base,
			Api::SearchBranch::Active,
			MakeFoundWithTraits(
				2,
				{ 102 },
				senderC,
				bit(Flag::File)));
		good &= Check(
			page.delta.messages == MakeIds({ 102, 201 })
				&& page.combined.committed.messages
					== MakeIds({ 101, 102, 201 })
				&& page.combined.heldMigrated.traits.empty()
				&& HasTraits(
					page.delta,
					MakeIds({ 102 })[0],
					{ senderC, bit(Flag::File) })
				&& HasTraits(
					page.delta,
					MakeIds({ 201 })[0],
					{ senderB, bit(Flag::Video) })
				&& page.combined.committed.traits.size() == 3,
			"held migrated traits join active exhaustion delta");
	}
	{
		auto base = Api::CombineSearchFirstPage(
			MakeFoundWithTraits(
				1,
				{ 101 },
				senderA,
				bit(Flag::Photo)),
			MakeFoundWithTraits(
				2,
				{ 201 },
				senderB,
				bit(Flag::Video)));
		const auto page = Api::CombineSearchPage(
			base,
			Api::SearchBranch::Migrated,
			MakeFoundWithTraits(
				2,
				{ 202 },
				senderC,
				bit(Flag::Poll)));
		good &= Check(
			page.delta.messages == MakeIds({ 202 })
				&& HasTraits(
					page.delta,
					MakeIds({ 202 })[0],
					{ senderC, bit(Flag::Poll) })
				&& HasTraits(
					page.combined.committed,
					MakeIds({ 202 })[0],
					{ senderC, bit(Flag::Poll) }),
			"migrated continuation traits merge with ids");
	}
	return good;
}

[[nodiscard]] bool CheckAdaptiveAuthoritativeProbeOverlap() {
	auto good = true;
	{
		auto state = Api::SearchIntersectionState();
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			{},
			PeerId(UserId(1)));
		(void)state.accept(
			Api::SearchIntersectionLeg::Sender,
			AdaptiveFound(
				action.senderRequest,
				5,
				MakeIds({ 50, 40, 40, 30, 20 })),
			MakeIds({ 50, 30, 30 }),
			true);
		const auto next = state.accept(
			Api::SearchIntersectionLeg::Filter,
			AdaptiveFound(
				action.filterRequest,
				10,
				MakeIds({ 60, 40, 40 })),
			MakeIds({ 40 }),
			false);
		good &= Check(
			state.driver() == Api::SearchIntersectionLeg::Sender
				&& next.outcome
				&& next.outcome->found.messages == MakeIds({ 50, 40, 30 })
				&& next.outcome->found.total == 3,
			"adaptive sender driver unions local matches with probe overlap");
	}
	{
		auto state = Api::SearchIntersectionState();
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			{},
			PeerId(UserId(1)));
		(void)state.accept(
			Api::SearchIntersectionLeg::Sender,
			AdaptiveFound(
				action.senderRequest,
				10,
				MakeIds({ 95, 90, 80 })),
			MakeIds({ 90, 80 }),
			false);
		const auto next = state.accept(
			Api::SearchIntersectionLeg::Filter,
			AdaptiveFound(
				action.filterRequest,
				3,
				MakeIds({ 90, 80, 70 })),
			MakeIds({ 70 }),
			true);
		good &= Check(
			state.driver() == Api::SearchIntersectionLeg::Filter
				&& next.outcome
				&& next.outcome->found.messages == MakeIds({ 90, 80, 70 })
				&& next.outcome->found.total == 3,
			"adaptive filter driver treats first-page overlap symmetrically");
	}
	return good;
}

[[nodiscard]] bool CheckAdaptiveDriverSelection() {
	auto good = true;
	{
		auto state = Api::SearchIntersectionState({ .pageSize = 2 });
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			{},
			PeerId(UserId(1)));
		auto next = state.accept(
			Api::SearchIntersectionLeg::Sender,
			AdaptiveFound(action.senderRequest, 3, MakeIds({ 30, 20 })),
			MakeIds({ 30, 20 }),
			false);
		good &= Check(!next.outcome, "adaptive probe waits for both legs");
		next = state.accept(
			Api::SearchIntersectionLeg::Filter,
			AdaptiveFound(action.filterRequest, 10, MakeIds({ 40, 30 })),
			MakeIds({ 30 }),
			false);
		good &= Check(
			state.driver() == Api::SearchIntersectionLeg::Sender
				&& next.outcome
				&& next.outcome->found.messages == MakeIds({ 30, 20 })
				&& next.outcome->found.manualContinuation
				&& !next.outcome->found.hasMore,
			"adaptive search chooses the smaller sender stream");
	}
	{
		auto state = Api::SearchIntersectionState({ .pageSize = 1 });
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			{},
			PeerId(UserId(1)));
		(void)state.accept(
			Api::SearchIntersectionLeg::Sender,
			AdaptiveFound(action.senderRequest, 5, MakeIds({ 30 })),
			MakeIds({ 30 }),
			false);
		const auto next = state.accept(
			Api::SearchIntersectionLeg::Filter,
			AdaptiveFound(action.filterRequest, 5, MakeIds({ 20 })),
			MakeIds({ 20 }),
			false);
		good &= Check(
			state.driver() == Api::SearchIntersectionLeg::Filter
				&& next.outcome
				&& next.outcome->found.messages == MakeIds({ 20 }),
			"adaptive tie chooses the filter stream");
	}
	{
		auto state = Api::SearchIntersectionState({ .pageSize = 1 });
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			{},
			PeerId(UserId(1)));
		(void)state.accept(
			Api::SearchIntersectionLeg::Sender,
			AdaptiveFound(action.senderRequest, -1, MakeIds({ 30 })),
			MakeIds({ 30 }),
			false);
		const auto next = state.accept(
			Api::SearchIntersectionLeg::Filter,
			AdaptiveFound(action.filterRequest, 5, MakeIds({ 20 })),
			MakeIds({ 20 }),
			false);
		good &= Check(
			state.driver() == Api::SearchIntersectionLeg::Filter
				&& next.outcome
				&& next.outcome->found.messages == MakeIds({ 20 }),
			"adaptive unknown total chooses the filter stream");
	}
	{
		auto state = Api::SearchIntersectionState({ .pageSize = 50 });
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			{},
			PeerId(UserId(1)));
		(void)state.accept(
			Api::SearchIntersectionLeg::Sender,
			AdaptiveFound(action.senderRequest, 2, MakeIds({ 30, 20 })),
			MakeIds({ 30 }),
			true);
		const auto next = state.accept(
			Api::SearchIntersectionLeg::Filter,
			AdaptiveFound(action.filterRequest, 4, MakeIds({ 40, 30 })),
			MakeIds({ 30 }),
			false);
		good &= Check(
			next.outcome
				&& next.outcome->found.messages == MakeIds({ 30 })
				&& next.outcome->found.total == 1
				&& !next.outcome->found.manualContinuation
				&& !next.outcome->found.partial,
			"adaptive exhausted driver publishes an exact local total");
	}
	{
		auto state = Api::SearchIntersectionState();
		auto action = state.begin(
			Api::AllocateSearchGeneration(),
			{},
			PeerId(UserId(1)));
		const auto next = state.accept(
			Api::SearchIntersectionLeg::Sender,
			AdaptiveFound(action.senderRequest, 0, {}),
			{},
			true);
		good &= Check(
			next.outcome
				&& next.outcome->type == Api::SearchOutcomeType::Empty
				&& next.outcome->found.total == 0
				&& !next.outcome->found.manualContinuation
				&& next.cancelFilter,
			"adaptive zero probe immediately proves an empty intersection");
	}
	return good;
}

[[nodiscard]] bool CheckAdaptiveContinuationAndCap() {
	auto good = Check(
		Api::SearchIntersectionRequestDelay(1000, 1500, 1000) == 500
			&& Api::SearchIntersectionRequestDelay(1000, 2000, 1000) == 0,
		"adaptive requests enforce a one-second minimum interval");
	auto state = Api::SearchIntersectionState({
		.pageSize = 3,
		.maxDriverPages = 3,
		.automaticDriverPages = 1,
	});
	auto action = state.begin(
		Api::AllocateSearchGeneration(),
		Api::SearchCriteria{ .hasFrom = true, .hasFilter = true },
		PeerId(UserId(1)),
		PeerId(UserId(2)));
	(void)state.accept(
		Api::SearchIntersectionLeg::Filter,
		AdaptiveFound(action.filterRequest, 10, MakePeerIds(1, { 90 })),
		MakePeerIds(1, { 90 }),
		false);
	auto next = state.accept(
		Api::SearchIntersectionLeg::Sender,
		AdaptiveFound(action.senderRequest, 20, MakePeerIds(1, { 95 })),
		{},
		false);
	good &= Check(
		!next.outcome
			&& next.filterRequest.generation
			&& !next.senderRequest.generation,
		"adaptive first page automatically requests one driver page");
	next = state.accept(
		Api::SearchIntersectionLeg::Filter,
		AdaptiveFound(next.filterRequest, 10, MakePeerIds(2, { 80 })),
		MakePeerIds(2, { 80 }),
		false);
	good &= Check(
		next.outcome
			&& next.outcome->page == Api::SearchPage::First
			&& next.outcome->found.messages
				== (MessageIdsList{ FullMsgId(PeerId(UserId(1)), MsgId(90)),
					FullMsgId(PeerId(UserId(2)), MsgId(80)) })
			&& next.outcome->found.total == -1
			&& next.outcome->found.manualContinuation
			&& state.canSearchMore(),
		"adaptive sparse first page requires manual continuation");

	const auto moreGeneration = Api::AllocateSearchGeneration();
	next = state.more(moreGeneration);
	good &= Check(
		!next.outcome && next.filterRequest.generation,
		"adaptive manual continuation requests exactly the driver leg");
	next = state.accept(
		Api::SearchIntersectionLeg::Filter,
		AdaptiveFound(next.filterRequest, 10, MakePeerIds(1, { 70 })),
		MakePeerIds(1, { 70 }),
		false);
	good &= Check(
		next.outcome
			&& next.outcome->page == Api::SearchPage::More
			&& next.outcome->found.messages == MakePeerIds(1, { 70 })
			&& next.outcome->found.partial
			&& !next.outcome->found.manualContinuation
			&& !state.canSearchMore(),
		"adaptive driver cap is terminal and explicitly incomplete");

	auto bounded = Api::SearchIntersectionState({ .pageSize = 1 });
	action = bounded.begin(
		Api::AllocateSearchGeneration(),
		{},
		PeerId(UserId(1)));
	(void)bounded.accept(
		Api::SearchIntersectionLeg::Sender,
		AdaptiveFound(action.senderRequest, 100, MakeIds({ 100 })),
		{},
		false);
	next = bounded.accept(
		Api::SearchIntersectionLeg::Filter,
		AdaptiveFound(action.filterRequest, 50, MakeIds({ 99 })),
		MakeIds({ 99 }),
		false);
	good &= Check(
		next.outcome && next.outcome->found.manualContinuation,
		"adaptive default cap begins with manual continuation");
	for (auto page = 2; page <= 16; ++page) {
		next = bounded.more(Api::AllocateSearchGeneration());
		if (!Check(
				next.filterRequest.generation,
				"adaptive cap continues only the chosen filter stream")) {
			good = false;
			break;
		}
		next = bounded.accept(
			Api::SearchIntersectionLeg::Filter,
			AdaptiveFound(
				next.filterRequest,
				50,
				MakeIds({ 100 - page })),
			MakeIds({ 100 - page }),
			false);
		good &= Check(
			next.outcome
				&& (page < 16
					? next.outcome->found.manualContinuation
					: (next.outcome->found.partial
						&& !next.outcome->found.manualContinuation)),
			"adaptive default cap stops after sixteen driver pages");
	}
	return good;
}

[[nodiscard]] bool CheckAdaptiveFailureAndCancellation() {
	auto state = Api::SearchIntersectionState();
	const auto generation = Api::AllocateSearchGeneration();
	auto action = state.begin(
		generation,
		Api::SearchCriteria{ .hasFrom = true, .hasFilter = true },
		PeerId(UserId(1)));
	const auto stale = action.senderRequest;
	auto next = state.accept(
		Api::SearchIntersectionLeg::Sender,
		Api::SearchOutcome::RpcFailure(
			action.senderRequest.generation,
			Api::SearchPage::First,
			{},
			u"SEARCH_QUERY_EMPTY"_q,
			400),
		{},
		false);
	auto good = Check(
		next.outcome
			&& next.outcome->type == Api::SearchOutcomeType::RpcFailure
			&& next.outcome->diagnostic.rpcType == u"SEARCH_QUERY_EMPTY"_q
			&& next.cancelFilter
			&& next.outcome->found.partial,
		"adaptive any 400 fails and cancels the sibling probe");
	good &= Check(
		!state.accept(
			Api::SearchIntersectionLeg::Sender,
			AdaptiveFound(stale, 1, MakeIds({ 1 })),
			MakeIds({ 1 }),
			true).outcome,
		"adaptive failure rejects stale callbacks");

	action = state.begin(
		Api::AllocateSearchGeneration(),
		{},
		PeerId(UserId(1)));
	next = state.timeout(state.generation());
	good &= Check(
		next.outcome
			&& next.outcome->type == Api::SearchOutcomeType::Timeout
			&& next.cancelSender
			&& next.cancelFilter,
		"adaptive watchdog terminates both probes once");
	good &= Check(
		!state.cancel(state.generation()).outcome,
		"adaptive duplicate terminal is rejected");

	action = state.begin(
		Api::AllocateSearchGeneration(),
		{},
		PeerId(UserId(1)));
	const auto oldSender = action.senderRequest;
	const auto replacement = state.begin(
		Api::AllocateSearchGeneration(),
		{},
		PeerId(UserId(1)));
	good &= Check(
		replacement.outcome
			&& replacement.outcome->type == Api::SearchOutcomeType::Cancelled
			&& replacement.cancelSender
			&& replacement.cancelFilter,
		"adaptive replacement cancels the stale generation");
	good &= Check(
		!state.accept(
			Api::SearchIntersectionLeg::Sender,
			AdaptiveFound(oldSender, 1, MakeIds({ 1 })),
			MakeIds({ 1 }),
			true).outcome,
		"adaptive replacement rejects a stale probe callback");
	const auto cancelled = state.cancel(state.generation());
	good &= Check(
		cancelled.outcome
			&& cancelled.outcome->type == Api::SearchOutcomeType::Cancelled
			&& cancelled.cancelSender
			&& cancelled.cancelFilter,
		"adaptive explicit cancellation terminates both probes");

	auto paged = Api::SearchIntersectionState({ .pageSize = 1 });
	action = paged.begin(
		Api::AllocateSearchGeneration(),
		{},
		PeerId(UserId(1)));
	(void)paged.accept(
		Api::SearchIntersectionLeg::Sender,
		AdaptiveFound(action.senderRequest, 10, MakeIds({ 10 })),
		{},
		false);
	next = paged.accept(
		Api::SearchIntersectionLeg::Filter,
		AdaptiveFound(action.filterRequest, 5, MakeIds({ 9 })),
		MakeIds({ 9 }),
		false);
	next = paged.more(Api::AllocateSearchGeneration());
	next = paged.accept(
		Api::SearchIntersectionLeg::Filter,
		Api::SearchOutcome::RpcFailure(
			next.filterRequest.generation,
			Api::SearchPage::More,
			{},
			u"FLOOD_WAIT_12"_q,
			420),
		{},
		false);
	good &= Check(
		next.outcome
			&& next.outcome->type == Api::SearchOutcomeType::RpcFailure
			&& next.outcome->diagnostic.rpcType == u"FLOOD_WAIT_12"_q
			&& next.outcome->found.partial
			&& !next.outcome->found.manualContinuation
			&& paged.messages().messages == MakeIds({ 9 })
			&& !paged.canSearchMore(),
		"adaptive page failure preserves committed results and closes paging");
	return good;
}

[[nodiscard]] bool RunTests() {
	auto good = CheckFilters();
	good &= CheckSearchSelectionPolicy();
	good &= CheckIntersectionRoutePolicy();
	good &= CheckIntersectionRequestSplit();
	good &= CheckDirectStates();
	good &= CheckDirectReplacement();
	good &= CheckMergedSuccessfulFirstPages();
	good &= CheckMergedFailure();
	good &= CheckMergedCancellationAndReplacement();
	good &= CheckMergedWatchdog();
	good &= CheckPagination();
	good &= CheckLocalFilterMatching();
	good &= CheckRawTraitsMatching();
	good &= CheckSearchTraitsMerge();
	good &= CheckAdaptiveAuthoritativeProbeOverlap();
	good &= CheckAdaptiveDriverSelection();
	good &= CheckAdaptiveContinuationAndCap();
	good &= CheckAdaptiveFailureAndCancellation();
	good &= CheckIntersectionCumulativeExhaustion();
	good &= CheckDiagnostics();
	return good;
}

} // namespace

int main(int argc, char *argv[]) {
	const auto application = QCoreApplication(argc, argv);
	return RunTests() ? 0 : 1;
}
