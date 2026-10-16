#pragma once

#include "ayu/features/aggregate/aggregate_model.h"
#include "ayu/features/aggregate/aggregate_settings.h"
#include "data/data_chat_filters.h"
#include "data/data_messages.h"
#include "mtproto/sender.h"
#include <set>

class History;

namespace Ayu::Aggregate {

[[nodiscard]] MessageKey Key(FullMsgId id);
[[nodiscard]] FullMsgId FullId(MessageKey key);
[[nodiscard]] SourceType Type(not_null<PeerData*> peer);
[[nodiscard]] Record Describe(not_null<HistoryItem*> item);

class Controller final {
public:
	Controller(not_null<Main::Session*> session, FilterId folder);
	~Controller();

	void refresh(TimeId snapshot = 0);
	void resume();
	void restoreSnapshot(const Controller &snapshot);
	std::shared_ptr<Controller> fork(bool unfiltered = false) const;
	Data::MessagesSlice mediaNavigationSlice(Mask categories, bool stream) const;
	void appendReply(FullMsgId id, FullMsgId target);
	[[nodiscard]] int sourceCount() const;
	void loadMore();
	void retry();
	void stop();
	void setFilters(Mask categories, Mask sources, bool newestFirst);
	void exclude(PeerId peer, bool excluded);
	[[nodiscard]] const std::vector<Segment> &segments() const;
	[[nodiscard]] const std::vector<PeerId> &sources() const;
	[[nodiscard]] Data::MessagesSlice slice(FullMsgId around = {}, int before = 128, int after = 128) const;
	[[nodiscard]] Data::MessagesSlice mediaSlice(Category category, Data::MessagePosition around, int limit) const;
	[[nodiscard]] int indexOf(FullMsgId id) const;
	[[nodiscard]] bool anchorUnavailable(FullMsgId id) const;
	[[nodiscard]] int loadedCount() const { return int(_order.size()); }
	[[nodiscard]] std::uint64_t generation() const { return _generation; }
	[[nodiscard]] const Segment *segmentFor(FullMsgId id) const;
	[[nodiscard]] bool loading() const;
	[[nodiscard]] bool hasMore() const;
	[[nodiscard]] bool failed() const;
	[[nodiscard]] bool hasNewMessages() const { return _hasNewMessages; }
	[[nodiscard]] bool stopped() const { return _stopped; }
	[[nodiscard]] TimeId snapshot() const;
	[[nodiscard]] rpl::producer<> changed() const;
	[[nodiscard]] rpl::producer<> newMessages() const;
	[[nodiscard]] QString title() const;

private:
	struct Source {
		PeerId peer;
		MsgId offset = 0;
		TimeId frontier = 0;
		mtpRequestId request = 0;
		bool exhausted = false;
		bool failed = false;
	};

	void requestDirectory();
	void finishDirectory();
	void pump();
	void requestHistory(Source &source);
	void rebuild();
	[[nodiscard]] bool contains(not_null<History*> history) const;
	[[nodiscard]] bool active(const Source &source) const;

	const not_null<Main::Session*> _session;
	const FilterId _folder = 0;
	MTP::Sender _api;
	FolderSettings _settings;
	Parameters _parameters;
	TimeId _snapshot = 0;
	std::uint64_t _generation = 0;
	std::uint64_t _mergeBatch = 0;
	std::map<PeerId, Source> _peers;
	std::vector<PeerId> _sources;
	std::map<MessageKey, Record> _records;
	std::set<MessageKey> _deleted;
	std::map<MessageKey, MessageKey> _replies;
	std::set<MessageKey> _newMessageIds;
	std::vector<Segment> _segments;
	std::vector<FullMsgId> _order;
	std::map<FullMsgId, int> _indices;
	std::map<FullMsgId, int> _segmentIndices;
	mtpRequestId _directoryRequest = 0;
	int _directoryFolder = 0;
	TimeId _directoryDate = 0;
	MsgId _directoryMessage = 0;
	PeerId _directoryPeer;
	bool _directoryReady = false;
	bool _fixedScope = false;
	std::vector<Data::ChatFilter> _fixedFilters;
	bool _directoryFailed = false;
	bool _stopped = false;
	bool _hasNewMessages = false;
	int _budget = 0;
	rpl::event_stream<> _changed;
	rpl::event_stream<> _newMessages;
	rpl::lifetime _lifetime;

};

[[nodiscard]] std::shared_ptr<Controller> ControllerFor(
	not_null<Main::Session*> session, FilterId folder);

} // namespace Ayu::Aggregate
