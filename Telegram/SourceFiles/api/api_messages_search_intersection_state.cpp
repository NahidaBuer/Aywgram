/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_intersection_state.h"

namespace Api {
namespace {

[[nodiscard]] bool IsSuccessful(SearchOutcomeType type) {
	return (type == SearchOutcomeType::Success)
		|| (type == SearchOutcomeType::Empty);
}

[[nodiscard]] SearchIntersectionLeg ChooseDriver(
		const FoundMessages &sender,
		const FoundMessages &filter) {
	return (sender.total >= 0
		&& filter.total >= 0
		&& sender.total < filter.total)
		? SearchIntersectionLeg::Sender
		: SearchIntersectionLeg::Filter;
}

[[nodiscard]] MessageIdsList ReconcileFirstPageMatches(
		const FoundMessages &driver,
		const MessageIdsList &localMatches,
		const FoundMessages &other) {
	const auto local = std::set<FullMsgId>(
		localMatches.begin(),
		localMatches.end());
	const auto authoritative = std::set<FullMsgId>(
		other.messages.begin(),
		other.messages.end());
	auto result = MessageIdsList();
	result.reserve(driver.messages.size());
	for (const auto message : driver.messages) {
		if (local.contains(message) || authoritative.contains(message)) {
			result.push_back(message);
		}
	}
	return result;
}

} // namespace

bool SearchIntersectionExhausted(const FoundMessages &committed) {
	return committed.total >= 0
		&& int(committed.messages.size()) >= committed.total;
}

crl::time SearchIntersectionRequestDelay(
		crl::time lastRequestAt,
		crl::time now,
		crl::time minimumInterval) {
	return (!lastRequestAt || now - lastRequestAt >= minimumInterval)
		? 0
		: minimumInterval - (now - lastRequestAt);
}

SearchIntersectionState::SearchIntersectionState(
		SearchIntersectionLimits limits)
: _limits(limits) {
	if (_limits.maxPagesPerLeg > 0) {
		_limits.maxDriverPages = _limits.maxPagesPerLeg;
	}
	Expects(_limits.pageSize > 0);
	Expects(_limits.maxDriverPages > 0);
	Expects(_limits.automaticDriverPages >= 0);
}

SearchIntersectionAction SearchIntersectionState::begin(
		SearchGeneration generation,
		SearchCriteria criteria,
		PeerId activePeer,
		PeerId migratedPeer) {
	Expects(generation != 0);
	Expects(activePeer != PeerId());
	auto result = _pending
		? finishCancelled(SearchOutcomeType::Cancelled)
		: SearchIntersectionAction();
	reset();
	_generation = generation;
	_page = SearchPage::First;
	_criteria = criteria;
	_activePeer = activePeer;
	_migratedPeer = migratedPeer;
	_pending = true;
	result.senderRequest = request(_sender);
	result.filterRequest = request(_filter);
	return result;
}

SearchIntersectionAction SearchIntersectionState::more(
		SearchGeneration generation) {
	if (_pending || !_canSearchMore || !_driver || !generation
		|| generation == _generation) {
		return {};
	}
	_generation = generation;
	_page = SearchPage::More;
	_pageMessages.clear();
	_pending = true;
	_canSearchMore = false;
	auto &selected = leg(*_driver);
	if (selected.pages >= _limits.maxDriverPages) {
		return finishCapped();
	}
	auto result = SearchIntersectionAction();
	if (*_driver == SearchIntersectionLeg::Sender) {
		result.senderRequest = request(selected);
	} else {
		result.filterRequest = request(selected);
	}
	return result;
}

SearchIntersectionAction SearchIntersectionState::accept(
		SearchIntersectionLeg which,
		const SearchOutcome &outcome,
		MessageIdsList matches,
		bool exhausted) {
	auto &accepted = leg(which);
	if (!_pending
		|| !accepted.pendingGeneration
		|| accepted.pendingGeneration != outcome.generation) {
		return {};
	}
	accepted.pendingGeneration = 0;
	if (!IsSuccessful(outcome.type)) {
		return fail(outcome);
	}
	accepted.exhausted = exhausted;
	if (!_driver) {
		accepted.first = outcome.found;
		accepted.firstMatches = std::move(matches);
		accepted.received = true;
	} else if (which != *_driver) {
		return finishPartial(
			SearchOutcomeType::RpcFailure,
			SearchDiagnostic{ .rpcType = u"SEARCH_DRIVER_INVALID"_q });
	} else if (!appendMatches(std::move(matches))) {
		return finishPartial(
			SearchOutcomeType::RpcFailure,
			SearchDiagnostic{ .rpcType = u"SEARCH_ORDER_INVALID"_q });
	}
	return process();
}

SearchIntersectionAction SearchIntersectionState::cancel(
		SearchGeneration generation) {
	return (_pending && generation == _generation)
		? finishCancelled(SearchOutcomeType::Cancelled)
		: SearchIntersectionAction();
}

SearchIntersectionAction SearchIntersectionState::timeout(
		SearchGeneration generation) {
	return (_pending && generation == _generation)
		? finishPartial(SearchOutcomeType::Timeout)
		: SearchIntersectionAction();
}

SearchIntersectionAction SearchIntersectionState::clear() {
	auto result = _pending
		? finishCancelled(SearchOutcomeType::Cancelled)
		: SearchIntersectionAction();
	reset();
	return result;
}

SearchIntersectionAction SearchIntersectionState::abandon() {
	if (!_pending) {
		return {};
	}
	_pending = false;
	_canSearchMore = false;
	return {
		.cancelSender = bool(base::take(_sender.pendingGeneration)),
		.cancelFilter = bool(base::take(_filter.pendingGeneration)),
	};
}

bool SearchIntersectionState::pending() const {
	return _pending;
}

bool SearchIntersectionState::canSearchMore() const {
	return _canSearchMore;
}

std::optional<SearchIntersectionLeg> SearchIntersectionState::driver() const {
	return _driver;
}

bool SearchIntersectionState::expects(
		SearchIntersectionLeg which,
		SearchGeneration generation) const {
	return _pending && generation
		&& leg(which).pendingGeneration == generation;
}

SearchGeneration SearchIntersectionState::generation() const {
	return _generation;
}

const FoundMessages &SearchIntersectionState::messages() const {
	return _committed;
}

SearchIntersectionAction SearchIntersectionState::process() {
	if (!_driver) {
		if ((_sender.received && _sender.first.total == 0)
			|| (_filter.received && _filter.first.total == 0)) {
			auto result = finishSuccess(true);
			result.cancelSender
				= bool(base::take(_sender.pendingGeneration));
			result.cancelFilter
				= bool(base::take(_filter.pendingGeneration));
			return result;
		}
		if (!_sender.received || !_filter.received) {
			return {};
		}
		_driver = ChooseDriver(_sender.first, _filter.first);
		auto &selected = leg(*_driver);
		const auto &other = leg(
			(*_driver == SearchIntersectionLeg::Sender)
				? SearchIntersectionLeg::Filter
				: SearchIntersectionLeg::Sender);
		if (!appendMatches(ReconcileFirstPageMatches(
			selected.first,
			selected.firstMatches,
			other.first))) {
			return finishPartial(
				SearchOutcomeType::RpcFailure,
				SearchDiagnostic{ .rpcType = u"SEARCH_ORDER_INVALID"_q });
		}
	}

	auto &selected = leg(*_driver);
	if (selected.exhausted) {
		return finishSuccess(true);
	}
	const auto automaticLimit = 1 + _limits.automaticDriverPages;
	if (_page == SearchPage::First
		&& int(_pageMessages.size()) < _limits.pageSize
		&& selected.pages < automaticLimit
		&& selected.pages < _limits.maxDriverPages) {
		auto result = SearchIntersectionAction();
		if (*_driver == SearchIntersectionLeg::Sender) {
			result.senderRequest = request(selected);
		} else {
			result.filterRequest = request(selected);
		}
		return result;
	}
	if (selected.pages >= _limits.maxDriverPages) {
		return finishCapped();
	}
	return finishSuccess(false);
}

SearchIntersectionAction SearchIntersectionState::fail(
		const SearchOutcome &outcome) {
	return finishPartial(outcome.type, outcome.diagnostic);
}

SearchIntersectionAction SearchIntersectionState::finishPartial(
		SearchOutcomeType type,
		const SearchDiagnostic &diagnostic) {
	Expects(type == SearchOutcomeType::RpcFailure
		|| type == SearchOutcomeType::Timeout
		|| type == SearchOutcomeType::Cancelled);
	auto found = FoundMessages{
		.total = -1,
		.messages = _pageMessages,
		.hasMore = false,
		.manualContinuation = false,
		.partial = true,
	};
	auto outcome = (type == SearchOutcomeType::RpcFailure)
		? SearchOutcome::RpcFailure(
			_generation,
			_page,
			_criteria,
			diagnostic.rpcType,
			diagnostic.rpcCode)
		: (type == SearchOutcomeType::Timeout)
			? SearchOutcome::Timeout(_generation, _page, _criteria)
			: SearchOutcome::Cancelled(_generation, _page, _criteria);
	outcome.found = found;
	appendCommitted();
	_committed.total = -1;
	_committed.hasMore = false;
	_committed.manualContinuation = false;
	_committed.partial = true;
	_pending = false;
	_canSearchMore = false;
	return {
		.outcome = std::move(outcome),
		.cancelSender = bool(base::take(_sender.pendingGeneration)),
		.cancelFilter = bool(base::take(_filter.pendingGeneration)),
	};
}

SearchIntersectionAction SearchIntersectionState::finishSuccess(
		bool complete) {
	auto found = FoundMessages{
		.total = complete
			? int(_committed.messages.size() + _pageMessages.size())
			: -1,
		.messages = _pageMessages,
		.hasMore = false,
		.manualContinuation = !complete,
		.partial = false,
	};
	appendCommitted();
	_committed.total = found.total;
	_committed.hasMore = false;
	_committed.manualContinuation = found.manualContinuation;
	_committed.partial = false;
	_pending = false;
	_canSearchMore = found.manualContinuation;
	return {
		.outcome = SearchOutcome::FromFound(
			_generation,
			_page,
			_criteria,
			std::move(found)),
	};
}

SearchIntersectionAction SearchIntersectionState::finishCapped() {
	auto result = finishSuccess(false);
	result.outcome->found.manualContinuation = false;
	result.outcome->found.partial = true;
	_committed.manualContinuation = false;
	_committed.partial = true;
	_canSearchMore = false;
	return result;
}

SearchIntersectionAction SearchIntersectionState::finishCancelled(
		SearchOutcomeType type) {
	Expects(type == SearchOutcomeType::Cancelled);
	_pending = false;
	_canSearchMore = false;
	return {
		.outcome = SearchOutcome::Cancelled(
			_generation,
			_page,
			_criteria),
		.cancelSender = bool(base::take(_sender.pendingGeneration)),
		.cancelFilter = bool(base::take(_filter.pendingGeneration)),
	};
}

bool SearchIntersectionState::appendMatches(MessageIdsList messages) {
	for (const auto message : messages) {
		if (message.peer != _activePeer
			&& (!_migratedPeer || message.peer != _migratedPeer)) {
			return false;
		}
		if (_matched.emplace(message).second) {
			_pageMessages.push_back(message);
		}
	}
	return true;
}

SearchIntersectionRequest SearchIntersectionState::request(LegState &value) {
	Expects(!value.pendingGeneration);
	Expects(value.pages < _limits.maxDriverPages);
	const auto result = SearchIntersectionRequest{
		.generation = AllocateSearchGeneration(),
		.first = (value.pages == 0),
	};
	value.pendingGeneration = result.generation;
	++value.pages;
	return result;
}

SearchIntersectionState::LegState &SearchIntersectionState::leg(
		SearchIntersectionLeg which) {
	return (which == SearchIntersectionLeg::Sender) ? _sender : _filter;
}

const SearchIntersectionState::LegState &SearchIntersectionState::leg(
		SearchIntersectionLeg which) const {
	return (which == SearchIntersectionLeg::Sender) ? _sender : _filter;
}

void SearchIntersectionState::appendCommitted() {
	_committed.messages.insert(
		end(_committed.messages),
		_pageMessages.begin(),
		_pageMessages.end());
	_pageMessages.clear();
}

void SearchIntersectionState::reset() {
	_generation = 0;
	_page = SearchPage::First;
	_criteria = {};
	_activePeer = {};
	_migratedPeer = {};
	_sender = {};
	_filter = {};
	_pageMessages.clear();
	_matched.clear();
	_committed = {};
	_driver.reset();
	_pending = false;
	_canSearchMore = false;
}

} // namespace Api
