/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_messages_search_state.h"

#include <optional>
#include <set>

namespace Api {

enum class SearchIntersectionLeg {
	Sender,
	Filter,
};

struct SearchIntersectionLimits {
	int pageSize = 50;
	int maxPagesPerLeg = 32;
};

struct SearchIntersectionRequest {
	SearchGeneration generation = 0;
	bool first = false;
};

struct SearchIntersectionAction {
	std::optional<SearchOutcome> outcome;
	SearchIntersectionRequest senderRequest;
	SearchIntersectionRequest filterRequest;
	bool cancelSender = false;
	bool cancelFilter = false;
};

[[nodiscard]] bool SearchIntersectionExhausted(
	const FoundMessages &committed);

class SearchIntersectionState final {
public:
	explicit SearchIntersectionState(SearchIntersectionLimits limits = {});

	[[nodiscard]] SearchIntersectionAction begin(
		SearchGeneration generation,
		SearchCriteria criteria,
		PeerId activePeer,
		PeerId migratedPeer = {});
	[[nodiscard]] SearchIntersectionAction more(
		SearchGeneration generation);
	[[nodiscard]] SearchIntersectionAction accept(
		SearchIntersectionLeg leg,
		const SearchOutcome &outcome,
		bool exhausted);
	[[nodiscard]] SearchIntersectionAction cancel(
		SearchGeneration generation);
	[[nodiscard]] SearchIntersectionAction timeout(
		SearchGeneration generation);
	[[nodiscard]] SearchIntersectionAction clear();
	[[nodiscard]] SearchIntersectionAction abandon();

	[[nodiscard]] bool pending() const;
	[[nodiscard]] bool canSearchMore() const;
	[[nodiscard]] bool expects(
		SearchIntersectionLeg leg,
		SearchGeneration generation) const;
	[[nodiscard]] SearchGeneration generation() const;
	[[nodiscard]] const FoundMessages &messages() const;

private:
	struct LegState {
		MessageIdsList buffer;
		std::set<FullMsgId> seen;
		std::optional<FullMsgId> last;
		size_t offset = 0;
		SearchGeneration pendingGeneration = 0;
		int pages = 0;
		bool exhausted = false;
	};

	[[nodiscard]] SearchIntersectionAction process();
	[[nodiscard]] SearchIntersectionAction fail(
		const SearchOutcome &outcome);
	[[nodiscard]] SearchIntersectionAction finishPartial(
		SearchOutcomeType type,
		const SearchDiagnostic &diagnostic = {});
	[[nodiscard]] SearchIntersectionAction finishSuccess(
		bool complete,
		bool capped = false);
	[[nodiscard]] SearchIntersectionAction finishCancelled(
		SearchOutcomeType type);
	[[nodiscard]] bool append(
		LegState &leg,
		MessageIdsList messages);
	[[nodiscard]] bool newer(FullMsgId a, FullMsgId b) const;
	[[nodiscard]] bool available(const LegState &leg) const;
	[[nodiscard]] SearchIntersectionRequest request(LegState &leg);
	[[nodiscard]] LegState &leg(SearchIntersectionLeg which);
	[[nodiscard]] const LegState &leg(SearchIntersectionLeg which) const;
	void appendCommitted();
	void reset();

	SearchIntersectionLimits _limits;
	SearchGeneration _generation = 0;
	SearchPage _page = SearchPage::First;
	SearchCriteria _criteria;
	PeerId _activePeer;
	PeerId _migratedPeer;
	LegState _sender;
	LegState _filter;
	MessageIdsList _pageMessages;
	std::set<FullMsgId> _matched;
	FoundMessages _committed;
	bool _pending = false;
	bool _canSearchMore = false;

};

} // namespace Api
