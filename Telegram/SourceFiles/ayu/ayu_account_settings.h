#pragma once

#include "ayu/libs/json.hpp"

namespace Main {
class Session;
} // namespace Main

namespace AyuAccountSettings {

[[nodiscard]] bool IgnoreRemoteText(not_null<Main::Session*> session);
[[nodiscard]] bool BlockLocalTextUpload(not_null<Main::Session*> session);
[[nodiscard]] bool IsolationEnabled(not_null<Main::Session*> session);

void SetIgnoreRemoteText(not_null<Main::Session*> session, bool value);
void SetBlockLocalTextUpload(not_null<Main::Session*> session, bool value);
[[nodiscard]] nlohmann::json CloudExport(
	not_null<Main::Session*> session);
[[nodiscard]] bool CloudValidate(const nlohmann::json &data);
bool CloudApply(
	not_null<Main::Session*> session,
	const nlohmann::json &data);

} // namespace AyuAccountSettings
