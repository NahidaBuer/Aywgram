// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "api/api_common.h"
#include "data/data_drafts.h"

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace AyuForward {

enum class Result {
	Success,
	Failure,
	Cancelled,
};
using CompletionCallback = FnMut<void(Result)>;

class ForwardState final {
public:
	enum class State {
		Preparing,
		Downloading,
		Sending,
		Finished,
	};

	void updateBottomBar(
		not_null<Main::Session*> session,
		PeerId peerId,
		State state);

	int totalChunks = 0;
	int currentChunk = 0;
	int totalMessages = 0;
	int sentMessages = 0;
	State state = State::Preparing;
	bool stopRequested = false;

};

[[nodiscard]] bool isForwarding(const PeerId &id);
void cancelForward(const PeerId &id, const Main::Session &session);
[[nodiscard]] std::pair<QString, QString> stateName(const PeerId &id);
[[nodiscard]] bool isAyuForwardNeeded(
	const std::vector<not_null<HistoryItem*>> &items);
[[nodiscard]] bool isAyuForwardNeeded(not_null<HistoryItem*> item);
[[nodiscard]] bool isFullAyuForwardNeeded(not_null<HistoryItem*> item);
void intelligentForward(
	not_null<Main::Session*> session,
	const Api::SendAction &action,
	const Data::ResolvedForwardDraft &draft,
	CompletionCallback completion = nullptr);
void forwardMessages(
	not_null<Main::Session*> session,
	const Api::SendAction &action,
	bool forwardState,
	const Data::ResolvedForwardDraft &draft,
	CompletionCallback completion = nullptr);

} // namespace AyuForward
