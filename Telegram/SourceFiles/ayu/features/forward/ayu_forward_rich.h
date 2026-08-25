// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "api/api_common.h"
#include "data/data_msg_id.h"

namespace Main {
class Session;
} // namespace Main

namespace AyuForward {

class RichForwardRequest {
public:
	virtual ~RichForwardRequest() = default;
	virtual void cancel() = 0;
};

[[nodiscard]] std::shared_ptr<RichForwardRequest> forwardRichMessage(
	not_null<Main::Session*> session,
	FullMsgId itemId,
	const Api::SendAction &action,
	FnMut<void(bool)> completion);

} // namespace AyuForward
