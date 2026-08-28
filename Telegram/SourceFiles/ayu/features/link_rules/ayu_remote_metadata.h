#pragma once

#include "base/basic_types.h"

namespace Main {
class Session;
} // namespace Main

namespace Ayu::RemoteMetadata {

inline constexpr auto kChannelUsername = "aywmeta";
inline constexpr uint64 kChannelId = 4142454792;

[[nodiscard]] bool Configured();
void Init();
void Refresh(
	not_null<Main::Session*> session,
	Fn<void(bool, QString)> done = nullptr);

} // namespace Ayu::RemoteMetadata
