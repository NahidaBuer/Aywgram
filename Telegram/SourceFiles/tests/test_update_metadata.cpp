#include "core/update_metadata.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <iostream>

namespace {

bool Check(bool condition, const char *message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}

QByteArray Metadata(const QJsonValue &asset, int schema = 1) {
	return QJsonDocument(QJsonObject{
		{ u"schema"_q, schema },
		{ u"feed_release"_q, u"update-metadata"_q },
		{ u"generated_at"_q, u"2026-09-01T12:00:00Z"_q },
		{ u"targets"_q, QJsonObject{
			{ u"linux-x86_64"_q, asset },
		} },
	}).toJson(QJsonDocument::Compact);
}

QJsonObject Asset(int appVersion = 7002000, int revision = 1) {
	return {
		{ u"app_version"_q, appVersion },
		{ u"revision"_q, revision },
		{ u"version_name"_q, u"7.2-1"_q },
		{ u"release"_q, u"pre-release-v7.2-1"_q },
		{ u"url"_q, u"https://github.com/NahidaBuer/AywGram/releases/"
			u"download/pre-release-v7.2-1/"
			u"AywGram-v7.2-1-linux-x86_64.tar.gz"_q },
		{ u"format"_q, u"tar.gz"_q },
		{ u"size"_q, 123456 },
		{ u"sha256"_q, QString(64, 'a') },
	};
}

bool RunTests() {
	auto good = true;
	good &= Check(Core::UpdateMetadata::TargetForPlatform(
		u"win64"_q, u"x86_64"_q) == u"windows-x86_64"_q,
		"windows target");
	good &= Check(Core::UpdateMetadata::TargetForPlatform(
		u"armac"_q, u"arm64"_q) == u"mac-arm64"_q,
		"mac target");
	good &= Check(Core::UpdateMetadata::TargetForPlatform(
		u"mac"_q, u"x86_64"_q).isEmpty(),
		"unsupported mac intel target");
	good &= Check(Core::UpdateMetadata::TargetForPlatform(
		u"linux"_q, u"aarch64"_q).isEmpty(),
		"unsupported linux architecture");
	good &= Check(Core::UpdateMetadata::ValidArchiveMemberPath(
		u"modules/x64/library.dll"_q), "valid archive path");
	good &= Check(!Core::UpdateMetadata::ValidArchiveMemberPath(
		u"../ready"_q), "archive path traversal");
	good &= Check(!Core::UpdateMetadata::ValidArchiveMemberPath(
		u"modules/../AywGram"_q), "embedded path traversal");
	good &= Check(!Core::UpdateMetadata::ValidArchiveMemberPath(
		u"C:\\AywGram.exe"_q), "archive drive escape");
	good &= Check(!Core::UpdateMetadata::ValidArchiveMemberPath(
		u"tdata/version"_q), "archive staging collision");
	auto valid = false;
	const auto parsed = Core::UpdateMetadata::Parse(
		Metadata(Asset()),
		u"linux-x86_64"_q,
		&valid);
	good &= Check(valid && parsed.has_value(), "valid metadata");
	good &= Check(parsed && parsed->appVersion == 7002000,
		"selected app version");
	good &= Check(parsed && parsed->revision == 1, "selected revision");
	good &= Check(Core::UpdateMetadata::ValidVersion(
		7002000, 1, u"7.2-1"_q), "valid short display version");
	good &= Check(Core::UpdateMetadata::ValidVersion(
		7002001, 99, u"7.2.1-99"_q), "valid precise display version");
	good &= Check(!Core::UpdateMetadata::ValidVersion(
		7002000, 0, u"7.2-0"_q), "revision zero");
	good &= Check(!Core::UpdateMetadata::ValidVersion(
		7002000, 1, u"7.2-2"_q), "revision name mismatch");
	good &= Check(Core::UpdateMetadata::IsNewer(
		{ 7001003, 2 }, { 7001003, 1 }), "revision update");
	good &= Check(Core::UpdateMetadata::IsNewer(
		{ 7001004, 1 }, { 7001003, 99 }), "base version update");
	good &= Check(!Core::UpdateMetadata::IsNewer(
		{ 7001003, 99 }, { 7001004, 1 }), "base version rollback");
	auto missingRevision = Asset();
	missingRevision.remove(u"revision"_q);
	good &= Check(!Core::UpdateMetadata::Parse(
		Metadata(missingRevision), u"linux-x86_64"_q),
		"missing revision");
	auto legacyVersion = Asset();
	legacyVersion.remove(u"app_version"_q);
	legacyVersion[u"version"_q] = 7002000;
	good &= Check(!Core::UpdateMetadata::Parse(
		Metadata(legacyVersion), u"linux-x86_64"_q),
		"legacy version field");
	good &= Check(!Core::UpdateMetadata::Parse(
		Metadata(Asset(), 2), u"linux-x86_64"_q), "unknown schema");
	auto invalidHash = Asset();
	invalidHash[u"sha256"_q] = QString(64, 'A');
	good &= Check(!Core::UpdateMetadata::Parse(
		Metadata(invalidHash), u"linux-x86_64"_q), "uppercase hash");
	auto invalidUrl = Asset();
	invalidUrl[u"url"_q] = u"https://api.github.com/repos/NahidaBuer/AywGram"_q;
	good &= Check(!Core::UpdateMetadata::Parse(
		Metadata(invalidUrl), u"linux-x86_64"_q), "api url");
	auto missingTargetValid = false;
	const auto missingTarget = QJsonDocument(QJsonObject{
		{ u"schema"_q, 1 },
		{ u"feed_release"_q, u"update-metadata"_q },
		{ u"generated_at"_q, u"2026-09-01T12:00:00Z"_q },
		{ u"targets"_q, QJsonObject() },
	}).toJson(QJsonDocument::Compact);
	good &= Check(!Core::UpdateMetadata::Parse(
		missingTarget, u"linux-x86_64"_q, &missingTargetValid)
		&& missingTargetValid, "missing target is valid metadata");
	return good;
}

} // namespace

int main(int argc, char *argv[]) {
	const auto application = QCoreApplication(argc, argv);
	return RunTests() ? 0 : 1;
}
