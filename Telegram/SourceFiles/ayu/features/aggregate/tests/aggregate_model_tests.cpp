#include "ayu/features/aggregate/aggregate_model.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace Ayu::Aggregate;

void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

Record Message(std::int64_t id, std::int64_t date, std::uint64_t peer = 1, SourceType source = SourceType::Group, std::uint64_t album = 0) {
	return { { peer, id }, { peer, 0, 0 }, date, album, source, Bit(Category::Text) };
}

void GapAndOrder() {
	const auto records = std::vector<Record>{ Message(3, 361), Message(2, 240), Message(1, 120), Message(1, 300, 2) };
	const auto segments = MakeSegments(records, {}, { { 1, true }, { 2, true } });
	Require(segments.size() == 3, "A gap equal to the threshold stays together; greater gaps split.");
	Require(segments[0].records[0].id.message == 3, "Newest segment first.");
	Require(segments[1].conversation.peer == 2, "Interleaved conversations stay separate.");
	Require(segments[2].records[0].id.message == 1, "Messages inside a segment are chronological.");
	const auto ascending = FilterSegments(segments, 0, 0, false);
	Require(ascending[0].records[0].id.message == 1 && ascending[0].records[1].id.message == 2, "Reverse only segments, never their messages.");
	auto zero = Parameters();
	zero.groupGap = 0;
	Require(MakeSegments({ Message(1, 1), Message(2, 1) }, zero, { { 1, true } }).size() == 1, "Zero gap still joins equal timestamps.");
}

void UnitsAndPagination() {
	auto records = std::vector<Record>();
	for (auto id = 1; id <= 31; ++id) {
		records.push_back(Message(id, id));
	}
	auto segments = MakeSegments(records, {}, { { 1, false } });
	Require(segments.size() == 2 && segments[0].records.size() == 30, "Thirty units counted from newest.");
	Require(segments[0].complete && !segments[1].complete, "A page boundary is not a conversation boundary.");
	records.back().album = 42;
	records.push_back(Message(32, 32, 1, SourceType::Group, 42));
	segments = MakeSegments(records, {}, { { 1, true } });
	Require(segments[0].records.size() == 31 && segments[0].units == 30, "An album is an indivisible display unit.");
	const auto part = std::vector<Record>{ Message(3, 3, 1, SourceType::Channel, 8), Message(2, 2, 1, SourceType::Channel, 8) };
	Require(!MakeSegments(part, {}, { { 1, false } })[0].complete, "Do not publish a partial album at the end of a history page.");
	auto complete = part;
	complete.push_back(Message(1, 1, 1, SourceType::Channel));
	const auto joined = MakeSegments(complete, {}, { { 1, true } });
	Require(joined.size() == 2 && joined[0].records.size() == 2, "A channel album remains a single post across pages.");
}

void TopicsAndFilters() {
	auto records = std::vector<Record>{ Message(1, 1), Message(2, 2), Message(3, 3), Message(4, 200), Message(5, 201) };
	records[1].conversation.topic = 7;
	records[4].conversation.sublist = 8;
	records[2].categories = Bit(Category::Photo) | Bit(Category::Link);
	records[3].categories = Bit(Category::Video);
	const auto segments = MakeSegments(records, {}, { { 1, true } });
	Require(segments.size() == 4, "Topics and direct-message sublists have independent contexts.");
	const auto photos = FilterSegments(segments, Bit(Category::Photo) | Bit(Category::Link), 0, true);
	Require(photos.size() == 1 && photos[0].records.size() == 1, "Overlapping categories do not duplicate messages.");
	Require(FilterSegments(segments, Bit(Category::Photo), Bit(SourceType::Bot), true).empty(), "Source and message filters intersect.");
	auto albums = std::vector<Record>{ Message(1, 1, 1, SourceType::Channel, 4), Message(2, 2, 1, SourceType::Channel, 4) };
	albums[0].categories = Bit(Category::Photo);
	albums[1].categories = Bit(Category::Video);
	const auto filtered = FilterSegments(MakeSegments(albums, {}, { { 1, true } }), Bit(Category::Video), 0, true);
	Require(filtered[0].records.size() == 2, "A matching album member shows the complete album.");
	Require(Toggle(Bit(Category::Video), Bit(Category::Video)) == 0 && Matches(0, Bit(Category::Text)), "Removing the last filter returns to all.");
}

void ConfirmedReplies() {
	auto records = std::vector<Record>();
	for (auto i = 1; i <= 30; ++i) {
		records.push_back(Message(i, i));
	}
	auto segments = MakeSegments(records, {}, { { 1, true } });
	const auto anchor = segments.front().anchor;
	const auto newest = segments.front().newest;
	AppendConfirmedReply(segments, Message(31, 9000), { 1, 1 });
	AppendConfirmedReply(segments, Message(32, 9001), { 1, 31 });
	Require(segments.size() == 1 && segments.front().records.size() == 32,
		"Confirmed replies bypass the segment cap and gap, including replies to replies.");
	Require(segments.front().anchor == anchor && segments.front().newest == newest,
		"Appending a reply keeps the snapshot segment's position stable.");
	AppendConfirmedReply(segments, Message(31, 9000), { 1, 1 });
	Require(segments.front().records.size() == 32, "Repeated confirmation does not duplicate a reply.");
	for (const auto newestFirst : { true, false }) {
		const auto shown = FilterSegments(segments, 0, 0, newestFirst);
		Require(shown.front().records.back().id.message == 32,
			"Replies remain in chronological order for either segment direction.");
	}
	auto otherTopic = Message(33, 9002);
	otherTopic.conversation.topic = 5;
	AppendConfirmedReply(segments, otherTopic, { 1, 1 });
	Require(segments.size() == 2, "Replies never merge distinct topics.");
	AppendConfirmedReply(segments, Message(-1, 9003), { 1, 1 });
	Require(segments.size() == 2 && segments.front().records.size() == 32,
		"Unconfirmed local messages are not inserted.");
}

void MediaNavigationOrder() {
	auto records = std::vector<Record>{ Message(1, 1), Message(2, 3), Message(1, 2, 2), Message(3, 4) };
	records[0].categories = Bit(Category::Photo);
	records[1].categories = Bit(Category::Video);
	records[2].categories = Bit(Category::Photo);
	const auto segments = MakeSegments(records, {}, { { 1, true }, { 2, true } });
	const auto visual = Bit(Category::Photo) | Bit(Category::Video);
	Require(MediaOrder(segments, visual) == std::vector<MessageKey>{ { 1, 1 }, { 1, 2 }, { 2, 1 } },
		"Viewer follows displayed segments, not a global timestamp sort, and keeps full peer/message IDs.");
	Require(MediaOrder(FilterSegments(segments, 0, 0, false), visual)
		== std::vector<MessageKey>{ { 2, 1 }, { 1, 1 }, { 1, 2 } },
		"Changing segment direction never reverses media inside a conversation.");
	records[0].album = records[1].album = 42;
	const auto albums = MakeSegments(records, {}, { { 1, true }, { 2, true } });
	const auto filtered = FilterSegments(albums, Bit(Category::Photo), 0, true);
	Require(MediaOrder(filtered, visual) == std::vector<MessageKey>{ { 1, 1 }, { 1, 2 }, { 2, 1 } },
		"A photo filter keeps the matching album's video in viewer navigation.");
	Require(MediaOrder(filtered, Bit(Category::Video)) == std::vector<MessageKey>{ { 1, 2 } },
		"Viewer category selection excludes unrelated media without duplicates.");
}

void AdjacentChannels() {
	const auto raw = MakeSegments({
		Message(5, 50, 1, SourceType::Channel),
		Message(4, 40, 1, SourceType::Channel),
		Message(3, 30, 2, SourceType::Channel),
		Message(2, 20, 1, SourceType::Channel),
		Message(1, 10, 1, SourceType::Channel),
	}, {}, { { 1, true }, { 2, true } });
	const auto merged = MergeAdjacentChannels(raw);
	Require(merged.size() == 3, "Only adjacent same-channel segments merge.");
	Require(merged[0].records[0].id.message == 4 && merged[0].records[1].id.message == 5,
		"A merged channel is genuinely chronological, not just missing separators.");
	Require(merged[0].anchor == raw[0].anchor && merged[0].newest == 50, "Keep the newest segment identity for sorting.");
	const auto reversed = FilterSegments(merged, 0, 0, false);
	Require(reversed[0].records[0].id.message == 1 && reversed[0].records[1].id.message == 2,
		"Reversing segment order preserves chronology within merged channels.");
	auto many = std::vector<Record>();
	for (auto i = 1; i <= 65; ++i) many.push_back(Message(i, i, 1, SourceType::Channel));
	Require(MergeAdjacentChannels(MakeSegments(many, {}, {{1, true}}))[0].records.size() == 65,
		"Channel runs may exceed the conversation segment limit.");
	auto withAlbum = MakeSegments({
		Message(1, 10, 1, SourceType::Channel, 8),
		Message(2, 10, 1, SourceType::Channel, 8),
		Message(3, 11, 1, SourceType::Channel),
	}, {}, {{1, true}});
	const auto albumRun = MergeAdjacentChannels(withAlbum);
	Require(albumRun.size() == 1 && albumRun[0].units == 2 && albumRun[0].records.size() == 3,
		"Merging channels keeps albums intact and counts them as one unit.");
	auto hiddenMiddle = merged;
	hiddenMiddle[1].records[0].categories = Bit(Category::Photo);
	const auto filtered = FilterSegments(hiddenMiddle, Bit(Category::Text), 0, true);
	Require(filtered.size() == 2, "Filtering a separating source never retroactively joins channel runs.");
	const auto groups = MakeSegments({Message(1, 1), Message(2, 999)}, {}, {{1, true}});
	Require(MergeAdjacentChannels(groups).size() == 2, "Group context boundaries remain intact.");
}

void ChannelPaginationBatches() {
	auto records = std::vector<Record>{
		Message(101, 101, 1, SourceType::Channel),
		Message(102, 102, 1, SourceType::Channel),
		Message(103, 103, 1, SourceType::Channel),
	};
	const auto first = MergeAdjacentChannels(MakeSegments(records, {}, {{1, true}}));
	Require(first.size() == 1, "Initial channel batch really merges.");
	auto older = Message(100, 100, 1, SourceType::Channel);
	older.batch = 1;
	records.push_back(older);
	const auto next = MergeAdjacentChannels(MakeSegments(records, {}, {{1, true}}));
	Require(next.size() == 2, "Later history cannot extend a published channel run backwards.");
	for (const auto newestFirst : { true, false }) {
		const auto order = MediaOrder(FilterSegments(next, 0, 0, newestFirst), 0);
		const auto expected = newestFirst
			? std::vector<MessageKey>{ {1, 101}, {1, 102}, {1, 103}, {1, 100} }
			: std::vector<MessageKey>{ {1, 100}, {1, 101}, {1, 102}, {1, 103} };
		Require(order == expected, "Older media arrives at the advertised historical edge.");
	}
	older.id.message = 99;
	older.date = 99;
	older.album = 9;
	records.back().album = 9;
	records.push_back(older);
	const auto albums = MergeAdjacentChannels(MakeSegments(records, {}, {{1, true}}));
	Require(albums.back().units == 1 && albums.back().records.size() == 2,
		"Historical albums remain indivisible inside their new batch.");
}

void SnapshotUpdates() {
	Require(IsUnseenSnapshotUpdate(100, 100, false),
		"A new message in the snapshot second must prompt a refresh.");
	Require(IsUnseenSnapshotUpdate(101, 100, false),
		"A later new message must prompt a refresh.");
	Require(!IsUnseenSnapshotUpdate(99, 100, false),
		"Older history does not prompt a refresh.");
	Require(!IsUnseenSnapshotUpdate(100, 100, true),
		"A message already in the snapshot is not new.");
}

void ReadingRestoration() {
	Require(ReadingRestoreAction(false, false, true, false, true) == RestoreAction::Wait,
		"A pending page cannot discard the saved anchor.");
	Require(ReadingRestoreAction(false, false, false, false, true) == RestoreAction::LoadMore,
		"Finishing the first batch is not proof that an anchor was deleted.");
	Require(ReadingRestoreAction(false, false, false, true, true) == RestoreAction::Wait,
		"Network failure keeps the anchor for retry.");
	Require(ReadingRestoreAction(true, false, true, false, true) == RestoreAction::Apply,
		"Finding the anchor allows restoration without waiting for unrelated sources.");
	Require(ReadingRestoreAction(false, true, false, false, true) == RestoreAction::Apply,
		"A proven missing anchor may use a neighbouring message.");
	Require(ReadingRestoreAction(false, false, false, false, false) == RestoreAction::Apply,
		"Exhausted history permits fallback.");
}

void NavigationAndPaging() {
	Require(EntryIntent(false) == NavigationIntent::Restore, "Opening from a chat restores reading progress.");
	Require(EntryIntent(true) == NavigationIntent::RefreshLatest, "Repeated activation refreshes without needing a new-message notification.");
	auto gate = PagingGate();
	Require(!gate.take(false, false, false, true), "Away from history edge does not load.");
	Require(!gate.take(true, true, false, true), "In-flight requests are not duplicated.");
	Require(!gate.take(true, false, true, true), "Failures require retry.");
	Require(gate.take(true, false, false, true), "First edge fetch.");
	Require(gate.take(true, false, false, true), "A short batch may refill the viewport.");
	Require(!gate.take(true, false, false, true), "Sparse filters cannot scan history without bounds.");
	gate.reset();
	Require(gate.take(true, false, false, true), "A new gesture permits another bounded batch.");
	Require(!gate.take(true, false, false, false), "Exhausted history does not load.");
}

void StableIdsAndDefaults() {
	const auto records = std::vector<Record>{ Message(1, 1, 1, SourceType::Channel), Message(1, 1, 2, SourceType::Channel), Message(1, 1, 1, SourceType::Channel) };
	const auto segments = MakeSegments(records, {}, { { 1, true }, { 2, true } });
	Require(segments.size() == 2 && segments[0].conversation.peer == 2, "Use full message identifiers for deduplication and timestamp ties.");
	Require(Gap(SourceType::Private, {}) == 900 && Gap(SourceType::Bot, {}) == 900, "Private and bot defaults.");
	Require(Normalize({ -1, 90000, 0, 0 }) == Parameters{ 0, 86400, 0, 1 }, "Invalid persisted parameters are normalized.");
}

} // namespace

int main() {
	try {
		GapAndOrder();
		UnitsAndPagination();
		TopicsAndFilters();
		StableIdsAndDefaults();
		ConfirmedReplies();
		MediaNavigationOrder();
		NavigationAndPaging();
		AdjacentChannels();
		ChannelPaginationBatches();
		ReadingRestoration();
		SnapshotUpdates();
		std::cout << "Aggregate model tests passed.\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
