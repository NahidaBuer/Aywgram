#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <vector>

namespace Ayu::Aggregate {

[[nodiscard]] constexpr bool IsUnseenSnapshotUpdate(
		std::int64_t date,
		std::int64_t snapshot,
		bool present) {
	return !present && date >= snapshot;
}

enum class RestoreAction {
	Wait,
	LoadMore,
	Apply,
};

[[nodiscard]] RestoreAction ReadingRestoreAction(
	bool located,
	bool unavailable,
	bool loading,
	bool failed,
	bool hasMore);

enum class NavigationIntent {
	Restore,
	RefreshLatest,
	Conversation,
	LocateMessage,
};

class PagingGate final {
public:
	void reset() { _remaining = 2; }
	bool take(bool nearEdge, bool loading, bool failed, bool hasMore);
private:
	int _remaining = 2;

};

[[nodiscard]] constexpr NavigationIntent EntryIntent(bool alreadyOpen) {
	return alreadyOpen ? NavigationIntent::RefreshLatest : NavigationIntent::Restore;
}

enum class SourceType : std::uint8_t {
	Channel,
	Group,
	Private,
	Bot,
};

enum class Category : std::uint8_t {
	Text,
	Photo,
	Video,
	Music,
	Voice,
	File,
	Link,
	Gif,
	RoundVideo,
	Sticker,
	Poll,
	Location,
	Count,
};

using Mask = std::uint32_t;

[[nodiscard]] constexpr Mask Bit(Category value) {
	return Mask(1) << static_cast<unsigned>(value);
}

[[nodiscard]] constexpr Mask Bit(SourceType value) {
	return Mask(1) << static_cast<unsigned>(value);
}

[[nodiscard]] constexpr Mask Toggle(Mask selected, Mask bit) {
	return selected ^ bit;
}

[[nodiscard]] constexpr bool Matches(Mask selected, Mask traits) {
	return !selected || (selected & traits);
}

struct MessageKey {
	std::uint64_t peer = 0;
	std::int64_t message = 0;

	friend auto operator<=>(const MessageKey &, const MessageKey &) = default;
};

struct ConversationKey {
	std::uint64_t peer = 0;
	std::int64_t topic = 0;
	std::uint64_t sublist = 0;

	friend auto operator<=>(
		const ConversationKey &,
		const ConversationKey &) = default;
};

struct Record {
	MessageKey id;
	ConversationKey conversation;
	std::int64_t date = 0;
	std::uint64_t album = 0;
	SourceType source = SourceType::Private;
	Mask categories = 0;
	std::uint64_t batch = 0;
};

struct Parameters {
	int groupGap = 120;
	int privateGap = 900;
	int botGap = 900;
	int maximumUnits = 30;

	friend bool operator==(const Parameters &, const Parameters &) = default;
};

struct Segment {
	ConversationKey conversation;
	MessageKey anchor;
	std::int64_t newest = 0;
	std::vector<Record> records;
	int units = 0;
	bool complete = false;
};

[[nodiscard]] std::vector<Segment> MergeAdjacentChannels(std::vector<Segment> segments);

void AppendConfirmedReply(
	std::vector<Segment> &segments,
	const Record &reply,
	MessageKey target);

[[nodiscard]] Parameters Normalize(Parameters parameters);
[[nodiscard]] int Gap(SourceType type, const Parameters &parameters);
[[nodiscard]] std::vector<Segment> MakeSegments(
	const std::vector<Record> &records,
	const Parameters &parameters,
	const std::map<std::uint64_t, bool> &exhausted);
[[nodiscard]] std::vector<Segment> FilterSegments(
	const std::vector<Segment> &segments,
	Mask categories,
	Mask sources,
	bool newestFirst);

[[nodiscard]] std::vector<MessageKey> MediaOrder(const std::vector<Segment> &segments, Mask categories);

} // namespace Ayu::Aggregate
