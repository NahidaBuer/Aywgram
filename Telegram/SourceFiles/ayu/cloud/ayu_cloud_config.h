#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

namespace AyuCloud::Config {

inline constexpr uint64 kHelperBotId = 8955621559;
inline const auto kHelperBotUsername = QString("AywSettingsBot");

[[nodiscard]] inline bool Configured() {
	return kHelperBotId != 0 && !kHelperBotUsername.isEmpty();
}

} // namespace AyuCloud::Config
