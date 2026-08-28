#pragma once

#include "base/expected.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <cstdint>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace AyuCloud {

enum class Category : uint32_t {
	AyuGlobal = (1U << 0),
	TelegramGlobal = (1U << 1),
	AccountAndChats = (1U << 2),
	Appearance = (1U << 3),
};

inline constexpr auto kAllCategories = uint32_t(0x0F);

struct Snapshot {
	QByteArray canonical;
	QString hash;
	QString contentHash;
	uint32_t categories = kAllCategories;
	uint64_t accountId = 0;
	bool proxiesIncluded = false;
};

struct ApplyResult {
	bool success = false;
	std::vector<QString> warnings;
};

enum class Difference {
	Same,
	Different,
	LocalOnly,
	RemoteOnly,
};

struct CategoryDifference {
	Category category = Category::AyuGlobal;
	Difference difference = Difference::Same;
};

[[nodiscard]] base::expected<Snapshot, QString> ExportSettingsSnapshot(
	not_null<Main::Session*> session,
	uint32_t categories,
	const QString &deviceId,
	bool includeProxies);
[[nodiscard]] base::expected<Snapshot, QString> ValidateSettingsSnapshot(
	const QByteArray &canonical,
	uint64_t expectedAccountId);
[[nodiscard]] base::expected<std::vector<CategoryDifference>, QString>
DiffSettingsSnapshots(const Snapshot &local, const Snapshot &remote);
[[nodiscard]] ApplyResult ApplySettingsSnapshot(
	not_null<Main::Session*> session,
	const Snapshot &snapshot);

} // namespace AyuCloud
