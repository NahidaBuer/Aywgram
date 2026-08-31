#include "core/update_metadata.h"

#include "base/platform/base_platform_info.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

#include <cmath>
#include <limits>

namespace Core::UpdateMetadata {
namespace {

constexpr auto kSchema = 1;

QString ExpectedFormat(const QString &target) {
	return target.startsWith(u"linux-"_q) ? u"tar.gz"_q : u"zip"_q;
}

bool ValidTarget(const QString &target) {
	return (target == u"windows-x86_64"_q)
		|| (target == u"windows-arm64"_q)
		|| (target == u"mac-arm64"_q)
		|| (target == u"linux-x86_64"_q);
}

bool ValidDownloadUrl(
		const QString &url,
		const QString &release,
		const QString &target,
		const QString &versionName) {
	const auto parsed = QUrl(url);
	const auto prefix = u"/NahidaBuer/AywGram/releases/download/"_q
		+ release
		+ '/';
	const auto suffix = parsed.path().mid(prefix.size());
	const auto extension = target.startsWith(u"linux-"_q)
		? u"tar.gz"_q
		: u"zip"_q;
	const auto expected = u"AywGram-v"_q
		+ versionName
		+ '-'
		+ target
		+ '.'
		+ extension;
	return parsed.isValid()
		&& (parsed.scheme() == u"https"_q)
		&& (parsed.host() == u"github.com"_q)
		&& parsed.path().startsWith(prefix)
		&& (suffix == expected)
		&& !parsed.hasQuery()
		&& !parsed.hasFragment();
}

std::optional<Asset> ParseAsset(
		const QJsonObject &object,
		const QString &target) {
	const auto appVersionValue = object.value(u"app_version"_q);
	const auto appVersionDouble = appVersionValue.toDouble();
	const auto revisionValue = object.value(u"revision"_q);
	const auto revisionDouble = revisionValue.toDouble();
	const auto versionName = object.value(u"version_name"_q).toString();
	const auto release = object.value(u"release"_q).toString();
	const auto url = object.value(u"url"_q).toString();
	const auto format = object.value(u"format"_q).toString();
	const auto sizeValue = object.value(u"size"_q);
	const auto sizeDouble = sizeValue.toDouble();
	const auto sha256 = object.value(u"sha256"_q).toString().toLatin1();
	const auto hashPattern = QRegularExpression(u"^[0-9a-f]{64}$"_q);
	const auto releasePattern = QRegularExpression(
		u"^[0-9A-Za-z][0-9A-Za-z._-]*$"_q);
	if (!appVersionValue.isDouble()
		|| !std::isfinite(appVersionDouble)
		|| (appVersionDouble < 1.)
		|| (appVersionDouble > double(std::numeric_limits<int32>::max()))
		|| (appVersionDouble != std::trunc(appVersionDouble))
		|| !revisionValue.isDouble()
		|| !std::isfinite(revisionDouble)
		|| (revisionDouble != std::trunc(revisionDouble))
		|| !ValidVersion(
			int(appVersionDouble),
			int(revisionDouble),
			versionName)
		|| (release != u"pre-release-v"_q + versionName)
		|| !releasePattern.match(release).hasMatch()
		|| !ValidDownloadUrl(url, release, target, versionName)
		|| (format != ExpectedFormat(target))
		|| !sizeValue.isDouble()
		|| !std::isfinite(sizeDouble)
		|| (sizeDouble < 1.)
		|| (sizeDouble > double(kMaximumArchiveSize))
		|| (sizeDouble != std::trunc(sizeDouble))
		|| !hashPattern.match(QString::fromLatin1(sha256)).hasMatch()) {
		return std::nullopt;
	}
	const auto size = int64(sizeDouble);
	return Asset{
		.appVersion = int(appVersionDouble),
		.revision = int(revisionDouble),
		.versionName = versionName,
		.release = release,
		.url = url,
		.format = format,
		.size = size,
		.sha256 = sha256,
	};
}

} // namespace

QString TargetForPlatform(
		const QString &key,
		const QString &architecture) {
	if (key == u"win64"_q) {
		return u"windows-x86_64"_q;
	} else if (key == u"winarm"_q) {
		return u"windows-arm64"_q;
	} else if (key == u"armac"_q) {
		return u"mac-arm64"_q;
	} else if (key == u"linux"_q
		&& (architecture.startsWith(u"x86_64"_q)
			|| architecture.startsWith(u"amd64"_q))) {
		return u"linux-x86_64"_q;
	}
	return {};
}

QString CurrentTarget() {
	return TargetForPlatform(
		Platform::AutoUpdateKey(),
		QSysInfo::currentCpuArchitecture());
}

bool ValidArchiveMemberPath(const QString &member) {
	if (member.isEmpty()
		|| member.startsWith('/')
		|| member.startsWith('\\')
		|| member.contains(':')
		|| member.contains(QChar(0))) {
		return false;
	}
	const auto normalized = QString(member).replace('\\', '/');
	const auto rawParts = normalized.split('/', Qt::SkipEmptyParts);
	if (rawParts.contains(u".."_q)) {
		return false;
	}
	const auto clean = QDir::cleanPath(normalized);
	if (clean == u".."_q
		|| clean.startsWith(u"../"_q)
		|| clean.startsWith('/')) {
		return false;
	}
	const auto parts = clean.split('/', Qt::SkipEmptyParts);
	return !parts.empty()
		&& !parts.contains(u".."_q)
		&& (parts.front().compare(u"tdata"_q, Qt::CaseInsensitive) != 0)
		&& (parts.back().compare(u"ready"_q, Qt::CaseInsensitive) != 0)
		&& (parts.back().compare(
			u"update-metadata.json"_q,
			Qt::CaseInsensitive) != 0);
}

bool ValidVersion(
		int appVersion,
		int revision,
		const QString &versionName) {
	const auto pattern = QRegularExpression(
		u"^([0-9]|[1-9][0-9]{0,2})\\.([0-9]|[1-9][0-9]{0,2})"
		u"(?:\\.([0-9]|[1-9][0-9]{0,2}))?-([1-9]|[1-9][0-9])$"_q);
	const auto match = pattern.match(versionName);
	if (!match.hasMatch() || revision != match.captured(4).toInt()) {
		return false;
	}
	const auto major = match.captured(1).toInt();
	const auto minor = match.captured(2).toInt();
	const auto patch = match.captured(3).isEmpty()
		? 0
		: match.captured(3).toInt();
	return revision >= 1
		&& revision <= 99
		&& appVersion == major * 1000000 + minor * 1000 + patch;
}

bool IsNewer(Version candidate, Version current) {
	return (candidate.appVersion > current.appVersion)
		|| ((candidate.appVersion == current.appVersion)
			&& (candidate.revision > current.revision));
}

std::optional<Asset> Parse(
		const QByteArray &json,
		const QString &target,
		bool *valid) {
	if (valid) {
		*valid = false;
	}
	if (json.isEmpty()
		|| (json.size() > kMaximumMetadataSize)
		|| !ValidTarget(target)) {
		return std::nullopt;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return std::nullopt;
	}
	const auto root = document.object();
	const auto feedRelease = root.value(u"feed_release"_q).toString();
	const auto generatedAt = root.value(u"generated_at"_q).toString();
	const auto releasePattern = QRegularExpression(
		u"^[0-9A-Za-z][0-9A-Za-z._-]*$"_q);
	const auto generated = QDateTime::fromString(generatedAt, Qt::ISODate);
	if (!root.value(u"schema"_q).isDouble()
		|| root.value(u"schema"_q).toDouble() != kSchema
		|| !releasePattern.match(feedRelease).hasMatch()
		|| !generated.isValid()
		|| generated.timeSpec() != Qt::UTC
		|| !root.value(u"targets"_q).isObject()) {
		return std::nullopt;
	}
	const auto value = root.value(u"targets"_q).toObject().value(target);
	if (value.isUndefined()) {
		if (valid) {
			*valid = true;
		}
		return std::nullopt;
	}
	if (!value.isObject()) {
		return std::nullopt;
	}
	const auto result = ParseAsset(value.toObject(), target);
	if (result && valid) {
		*valid = true;
	}
	return result;
}

} // namespace Core::UpdateMetadata
