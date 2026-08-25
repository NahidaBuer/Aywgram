/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_intersection_state.h"

#include <algorithm>

namespace Api {
namespace {

[[nodiscard]] bool IsSuccessful(SearchOutcomeType type) {
	return (type == SearchOutcomeType::Success)
		|| (type == SearchOutcomeType::Empty);
}

} // namespace

bool SearchIntersectionExhausted(const FoundMessages &committed) {
	return committed.total >= 0
		&& int(committed.messages.size()) >= committed.total;
}

SearchIntersectionState::SearchIntersectionState(
		SearchIntersectionLimits limits)
: _limits(limits) {
	Expects(_limits.pageSize > 0);
	Expects(_limits.maxPagesPerLeg > 0);
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
	if (_pending || !_canSearchMore || !generation
		|| generation == _generation) {
		return {};
	}
	_generation = generation;
	_page = SearchPage::More;
	_pageMessages.clear();
	_pending = true;
	_canSearchMore = false;
	return process();
}

SearchIntersectionAction SearchIntersectionState::accept(
		SearchIntersectionLeg which,
		const SearchOutcome &outcome,
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
	if (!append(accepted, outcome.found.messages)) {
		return finishPartial(
			SearchOutcomeType::RpcFailure,
			SearchDiagnostic{ .rpcType = u"SEARCH_ORDER_INVALID"_q });
	}
	accepted.exhausted = exhausted;
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
	while (int(_pageMessages.size()) < _limits.pageSize) {
		while (available(_sender) && available(_filter)) {
			const auto sender = _sender.buffer[_sender.offset];
			const auto filter = _filter.buffer[_filter.offset];
			if (sender == filter) {
				++_sender.offset;
				++_filter.offset;
				if (_matched.emplace(sender).second) {
					_pageMessages.push_back(sender);
					if (int(_pageMessages.size()) >= _limits.pageSize) {
						break;
					}
				}
			} else if (newer(sender, filter)) {
				++_sender.offset;
			} else {
				++_filter.offset;
			}
		}

		const auto senderDone = !available(_sender) && _sender.exhausted;
		const auto filterDone = !available(_filter) && _filter.exhausted;
		if (senderDone || filterDone) {
			return finishSuccess(true);
		}
		if (int(_pageMessages.size()) >= _limits.pageSize) {
			return finishSuccess(false);
		}

		auto result = SearchIntersectionAction();
		if (!available(_sender) && !_sender.pendingGeneration) {
			if (_sender.pages >= _limits.maxPagesPerLeg) {
				return finishSuccess(false, true);
			}
			result.senderRequest = request(_sender);
		}
		if (!available(_filter) && !_filter.pendingGeneration) {
			if (_filter.pages >= _limits.maxPagesPerLeg) {
				return finishSuccess(false, true);
			}
			result.filterRequest = request(_filter);
		}
		if (result.senderRequest.generation
			|| result.filterRequest.generation) {
			return result;
		}
		return {};
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
		bool complete,
		bool capped) {
	auto found = FoundMessages{
		.total = complete
			? int(_committed.messages.size() + _pageMessages.size())
			: -1,
		.messages = _pageMessages,
		.hasMore = !complete && !capped,
		.partial = capped,
	};
	appendCommitted();
	_committed.total = found.total;
	_committed.hasMore = found.hasMore;
	_committed.partial = found.partial;
	_pending = false;
	_canSearchMore = found.hasMore && !found.partial;
	auto outcome = SearchOutcome::FromFound(
		_generation,
		_page,
		_criteria,
		std::move(found));
	if (outcome.found.partial && outcome.found.messages.empty()) {
		outcome.found.total = -1;
	}
	return { .outcome = std::move(outcome) };
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

bool SearchIntersectionState::append(
		LegState &accepted,
		MessageIdsList messages) {
	for (const auto message : messages) {
		if (message.peer != _activePeer
			&& (!_migratedPeer || message.peer != _migratedPeer)) {
			return false;
		}
	}
	std::sort(messages.begin(), messages.end(), [=](FullMsgId a, FullMsgId b) {
		return newer(a, b);
	});
	for (const auto message : messages) {
		if (!accepted.seen.emplace(message).second) {
			continue;
		}
		if (accepted.last && newer(message, *accepted.last)) {
			return false;
		}
		accepted.buffer.push_back(message);
		accepted.last = message;
	}
	return true;
}

bool SearchIntersectionState::newer(FullMsgId a, FullMsgId b) const {
	const auto rank = [&](FullMsgId value) {
		return (value.peer == _activePeer)
			? 2
			: (value.peer == _migratedPeer) ? 1 : 0;
	};
	const auto aRank = rank(a);
	const auto bRank = rank(b);
	if (aRank != bRank) {
		return aRank > bRank;
	} else if (a.peer != b.peer) {
		return a.peer.value > b.peer.value;
	}
	return a.msg.bare > b.msg.bare;
}

bool SearchIntersectionState::available(const LegState &value) const {
	return value.offset < value.buffer.size();
}

SearchIntersectionRequest SearchIntersectionState::request(LegState &value) {
	Expects(!value.pendingGeneration);
	Expects(value.pages < _limits.maxPagesPerLeg);
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
		end(_pageMessages));
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
	_pending = false;
	_canSearchMore = false;
}

} // namespace Api
