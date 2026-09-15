#include "ayu/features/aggregate/aggregate_model.h"

#include <algorithm>
#include <set>
#include <tuple>

namespace Ayu::Aggregate {
namespace {

[[nodiscard]] auto Position(const Record &record) {
	return std::tuple(record.date, record.id);
}

[[nodiscard]] bool NewerSegment(const Segment &a, const Segment &b) {
	return std::tuple(a.newest, a.anchor) > std::tuple(b.newest, b.anchor);
}

} // namespace

RestoreAction ReadingRestoreAction(
		bool located,
		bool unavailable,
		bool loading,
		bool failed,
		bool hasMore) {
	if (located || unavailable) {
		return RestoreAction::Apply;
	} else if (loading || failed) {
		return RestoreAction::Wait;
	}
	return hasMore ? RestoreAction::LoadMore : RestoreAction::Apply;
}

bool PagingGate::take(bool nearEdge, bool loading, bool failed, bool hasMore) {
	if (!nearEdge || loading || failed || !hasMore || !_remaining) {
		return false;
	}
	--_remaining;
	return true;
}

std::vector<Segment> MergeAdjacentChannels(std::vector<Segment> segments) {
	std::sort(segments.begin(), segments.end(), NewerSegment);
	auto result = std::vector<Segment>();
	for (auto &segment : segments) {
		const auto channel = !segment.records.empty()
			&& segment.records.front().source == SourceType::Channel;
		if (channel && !result.empty()
			&& result.back().conversation == segment.conversation
			&& !result.back().records.empty()
			&& result.back().records.front().source == SourceType::Channel
			&& result.back().records.front().batch == segment.records.front().batch) {
			auto &joined = result.back();
			joined.units += segment.units;
			joined.complete = joined.complete && segment.complete;
			joined.records.insert(joined.records.end(), segment.records.begin(), segment.records.end());
		} else {
			result.push_back(std::move(segment));
		}
	}
	for (auto &segment : result) {
		std::sort(segment.records.begin(), segment.records.end(), [](const Record &a, const Record &b) {
			return Position(a) < Position(b);
		});
	}
	return result;
}

void AppendConfirmedReply(
		std::vector<Segment> &segments,
		const Record &reply,
		MessageKey target) {
	if (reply.id.message <= 0) {
		return;
	}
	for (auto &segment : segments) {
		if (std::any_of(segment.records.begin(), segment.records.end(), [&](const Record &record) {
			return record.id == reply.id;
		})) {
			return;
		}
	}
	for (auto &segment : segments) {
		if (segment.conversation == reply.conversation
			&& std::any_of(segment.records.begin(), segment.records.end(), [&](const Record &record) {
				return record.id == target;
			})) {
			segment.records.push_back(reply);
			std::sort(segment.records.begin(), segment.records.end(), [](const Record &a, const Record &b) {
				return Position(a) < Position(b);
			});
			return;
		}
	}
	segments.push_back(Segment{
		.conversation = reply.conversation,
		.anchor = reply.id,
		.newest = reply.date,
		.records = { reply },
		.units = 1,
		.complete = true,
	});
}

Parameters Normalize(Parameters parameters) {
	parameters.groupGap = std::clamp(parameters.groupGap, 0, 86400);
	parameters.privateGap = std::clamp(parameters.privateGap, 0, 86400);
	parameters.botGap = std::clamp(parameters.botGap, 0, 86400);
	parameters.maximumUnits = std::clamp(parameters.maximumUnits, 1, 500);
	return parameters;
}

int Gap(SourceType type, const Parameters &parameters) {
	switch (type) {
	case SourceType::Channel: return 0;
	case SourceType::Group: return parameters.groupGap;
	case SourceType::Private: return parameters.privateGap;
	case SourceType::Bot: return parameters.botGap;
	}
	return 0;
}

std::vector<Segment> MakeSegments(
		const std::vector<Record> &records,
		const Parameters &parameters,
		const std::map<std::uint64_t, bool> &exhausted) {
	const auto limits = Normalize(parameters);
	auto conversations = std::map<ConversationKey, std::vector<Record>>();
	auto seen = std::set<MessageKey>();
	for (const auto &record : records) {
		if (record.id.message > 0 && seen.insert(record.id).second) {
			conversations[record.conversation].push_back(record);
		}
	}
	auto result = std::vector<Segment>();
	for (auto &[key, messages] : conversations) {
		std::sort(messages.begin(), messages.end(), [](const auto &a, const auto &b) {
			return Position(a) > Position(b);
		});
		auto segment = Segment{ .conversation = key };
		const auto finish = [&](bool complete) {
			if (segment.records.empty()) {
				return;
			}
			segment.complete = complete;
			std::reverse(segment.records.begin(), segment.records.end());
			result.push_back(std::move(segment));
			segment = Segment{ .conversation = key };
		};
		for (const auto &message : messages) {
			const auto previous = segment.records.empty()
				? nullptr
				: &segment.records.back();
			const auto sameAlbum = previous
				&& message.album
				&& previous->album == message.album;
			const auto gap = Gap(message.source, limits);
			if (previous && !sameAlbum
				&& (message.source == SourceType::Channel
					|| previous->date - message.date > gap
					|| segment.units >= limits.maximumUnits)) {
				finish(true);
			}
			if (segment.records.empty()) {
				segment.anchor = message.id;
				segment.newest = message.date;
			}
			segment.records.push_back(message);
			if (!sameAlbum) {
				++segment.units;
			}
		}
		const auto i = exhausted.find(key.peer);
		finish(i != exhausted.end() && i->second);
	}
	std::sort(result.begin(), result.end(), NewerSegment);
	return result;
}

std::vector<Segment> FilterSegments(
		const std::vector<Segment> &segments,
		Mask categories,
		Mask sources,
		bool newestFirst) {
	auto result = std::vector<Segment>();
	for (const auto &segment : segments) {
		if (segment.records.empty()
			|| !Matches(sources, Bit(segment.records.front().source))) {
			continue;
		}
		auto albums = std::set<std::uint64_t>();
		for (const auto &record : segment.records) {
			if (record.album && Matches(categories, record.categories)) {
				albums.insert(record.album);
			}
		}
		auto filtered = segment;
		std::erase_if(filtered.records, [&](const Record &record) {
			return !Matches(categories, record.categories)
				&& (!record.album || !albums.contains(record.album));
		});
		if (!filtered.records.empty()) {
			result.push_back(std::move(filtered));
		}
	}
	std::sort(result.begin(), result.end(), NewerSegment);
	if (!newestFirst) {
		std::reverse(result.begin(), result.end());
	}
	return result;
}

std::vector<MessageKey> MediaOrder(const std::vector<Segment> &segments, Mask categories) {
	auto result = std::vector<MessageKey>();
	for (const auto &segment : segments) {
		for (const auto &record : segment.records) {
			if (Matches(categories, record.categories)) {
				result.push_back(record.id);
			}
		}
	}
	return result;
}

} // namespace Ayu::Aggregate
