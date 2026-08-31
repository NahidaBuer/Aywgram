#pragma once

#include "base/basic_types.h"

#include <optional>

namespace Core::UpdateMetadata {

inline constexpr auto kMaximumMetadataSize = 64 * 1024;
inline constexpr auto kMaximumArchiveSize = int64(1024) * 1024 * 1024;
inline constexpr auto kMaximumExpandedSize = int64(4) * 1024 * 1024 * 1024;

struct Asset {
	int appVersion = 0;
	int revision = 0;
	QString versionName;
	QString release;
	QString url;
	QString format;
	int64 size = 0;
	QByteArray sha256;
};

struct Version {
	int appVersion = 0;
	int revision = 0;
};

[[nodiscard]] QString CurrentTarget();
[[nodiscard]] QString TargetForPlatform(
	const QString &key,
	const QString &architecture);
[[nodiscard]] bool ValidArchiveMemberPath(const QString &member);
[[nodiscard]] bool ValidVersion(
	int appVersion,
	int revision,
	const QString &versionName);
[[nodiscard]] bool IsNewer(Version candidate, Version current);
[[nodiscard]] std::optional<Asset> Parse(
	const QByteArray &json,
	const QString &target,
	bool *valid = nullptr);

} // namespace Core::UpdateMetadata
