#pragma once

#include "api/api_common.h"

namespace Ayu::Aggregate {

struct ReplyJob {
	FullReplyTo reply;
	Api::SendOptions options;
	int count = 0;
	int sendingCount = 0;
	QByteArray draft;
	Fn<void(Api::SendAction, Api::SendCompletionPtr)> operation;
	bool sending = false;
	bool failed = false;
	rpl::event_stream<bool> completed;
};

[[nodiscard]] std::shared_ptr<ReplyJob> JobFor(not_null<Main::Session*> session, FilterId folder);
[[nodiscard]] bool ValidReply(not_null<Main::Session*> session, const FullReplyTo &reply);
void ConfirmReplyRead(not_null<Main::Session*> session, const FullReplyTo &reply);

} // namespace Ayu::Aggregate
