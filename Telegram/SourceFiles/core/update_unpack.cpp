#include "core/update_unpack.h"

#include "base/platform/base_platform_file_utilities.h"
#include "core/version.h"
#include "platform/platform_specific.h"
#include "settings.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QStandardPaths>

#include <minizip/unzip.h>

namespace Core {
namespace {

constexpr auto kStagingSchema = 1;

#ifdef Q_OS_WIN
using VersionInt = DWORD;
using VersionChar = WCHAR;
#else // Q_OS_WIN
using VersionInt = int;
using VersionChar = wchar_t;
#endif // Q_OS_WIN

QString UpdatesFolder() {
	return cWorkingDir() + u"tupdates"_q;
}

QString TempFolder() {
	return UpdatesFolder() + u"/temp"_q;
}

bool VerifyArchive(const QString &filepath, const UpdateMetadata::Asset &asset) {
	const auto info = QFileInfo(filepath);
	if (!info.isFile() || info.size() != asset.size) {
		LOG(("Update Error: archive size does not match metadata."));
		return false;
	}
	QFile input(filepath);
	if (!input.open(QIODevice::ReadOnly)) {
		return false;
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	while (!input.atEnd()) {
		const auto data = input.read(1024 * 1024);
		if (data.isEmpty() && input.error() != QFileDevice::NoError) {
			return false;
		}
		hash.addData(data);
	}
	if (hash.result().toHex() != asset.sha256) {
		LOG(("Update Error: archive SHA-256 does not match metadata."));
		return false;
	}
	return true;
}

#ifndef Q_OS_WIN
bool RunProcess(
		const QString &program,
		const QStringList &arguments,
		QByteArray *output = nullptr) {
	auto process = QProcess();
	process.setProgram(program);
	process.setArguments(arguments);
	process.setProcessChannelMode(QProcess::SeparateChannels);
	process.start();
	if (!process.waitForStarted() || !process.waitForFinished(-1)) {
		return false;
	}
	if (output) {
		*output = process.readAllStandardOutput();
	}
	return process.exitStatus() == QProcess::NormalExit
		&& process.exitCode() == 0;
}
#endif // !Q_OS_WIN

#if !defined Q_OS_WIN && !defined Q_OS_MAC
bool ValidateMemberList(const QByteArray &output) {
	const auto lines = QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts);
	auto members = QSet<QString>();
	if (lines.empty()) {
		return false;
	}
	for (const auto &line : lines) {
		const auto member = line.trimmed();
		const auto normalized = QDir::cleanPath(
			QString(member).replace('\\', '/'));
		if (!UpdateMetadata::ValidArchiveMemberPath(member)
			|| members.contains(normalized)) {
			return false;
		}
		members.insert(normalized);
	}
	return true;
}

bool ValidateTarSize(const QByteArray &output) {
	const auto expression = QRegularExpression(
		u"^\\S+\\s+\\S+\\s+(\\d+)\\s+\\d{4}-\\d{2}-\\d{2}\\s+"_q);
	auto expanded = int64(0);
	const auto lines = QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts);
	if (lines.empty()) {
		return false;
	}
	for (const auto &line : lines) {
		if (line.isEmpty()
			|| ((line.front() != '-') && (line.front() != 'd'))) {
			return false;
		}
		const auto match = expression.match(line);
		if (!match.hasMatch()) {
			return false;
		}
		auto ok = false;
		const auto size = match.captured(1).toLongLong(&ok);
		if (!ok || size < 0 || size > UpdateMetadata::kMaximumExpandedSize) {
			return false;
		}
		expanded += size;
		if (expanded > UpdateMetadata::kMaximumExpandedSize) {
			return false;
		}
	}
	return true;
}
#endif // !Q_OS_WIN && !Q_OS_MAC

bool ValidateZip(const QString &filepath) {
	const auto encoded = QFile::encodeName(filepath);
	const auto archive = unzOpen64(encoded.constData());
	if (!archive) {
		return false;
	}
	auto good = true;
	auto expanded = uint64(0);
	auto members = QSet<QString>();
	for (auto result = unzGoToFirstFile(archive); result == UNZ_OK;) {
		auto info = unz_file_info64();
		auto name = QByteArray(4096, Qt::Uninitialized);
		if (unzGetCurrentFileInfo64(
			archive,
			&info,
			name.data(),
			name.size(),
			nullptr,
			0,
			nullptr,
			0) != UNZ_OK
			|| info.size_filename >= uLong(name.size())) {
			good = false;
			break;
		}
		name.truncate(int(info.size_filename));
		const auto member = QString::fromUtf8(name);
		auto normalized = QDir::cleanPath(
			QString(member).replace('\\', '/'));
#if defined Q_OS_WIN || defined Q_OS_MAC
		normalized = normalized.toCaseFolded();
#endif // Q_OS_WIN || Q_OS_MAC
		const auto type = (info.external_fa >> 16) & 0170000;
		expanded += info.uncompressed_size;
		if (!UpdateMetadata::ValidArchiveMemberPath(member)
			|| members.contains(normalized)
			|| expanded > uint64(UpdateMetadata::kMaximumExpandedSize)) {
			good = false;
			break;
		}
		if (type
			&& type != 0100000
			&& type != 0040000
			&& type != 0120000) {
			good = false;
			break;
		}
#ifndef Q_OS_MAC
		if (type == 0120000) {
			good = false;
			break;
		}
#endif // !Q_OS_MAC
		members.insert(normalized);
		result = unzGoToNextFile(archive);
		if (result == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (result != UNZ_OK) {
			good = false;
			break;
		}
	}
	unzClose(archive);
	return good;
}

#ifdef Q_OS_WIN
bool ExtractZip(const QString &filepath, const QString &destination) {
	if (!ValidateZip(filepath)) {
		return false;
	}
	const auto encoded = QFile::encodeName(filepath);
	const auto archive = unzOpen64(encoded.constData());
	if (!archive) {
		return false;
	}
	auto good = true;
	auto expanded = uint64(0);
	for (auto result = unzGoToFirstFile(archive); result == UNZ_OK;) {
		auto info = unz_file_info64();
		auto name = QByteArray(4096, Qt::Uninitialized);
		if (unzGetCurrentFileInfo64(
			archive,
			&info,
			name.data(),
			name.size(),
			nullptr,
			0,
			nullptr,
			0) != UNZ_OK) {
			good = false;
			break;
		}
		if (info.size_filename >= uLong(name.size())) {
			good = false;
			break;
		}
		name.truncate(int(info.size_filename));
		const auto member = QString::fromUtf8(name);
		expanded += info.uncompressed_size;
		if (!UpdateMetadata::ValidArchiveMemberPath(member)
			|| expanded > uint64(UpdateMetadata::kMaximumExpandedSize)) {
			good = false;
			break;
		}
		const auto outputPath = destination + '/' + member;
		if (member.endsWith('/')) {
			good = QDir().mkpath(outputPath);
		} else {
			good = QDir().mkpath(QFileInfo(outputPath).absolutePath())
				&& (unzOpenCurrentFile(archive) == UNZ_OK);
			if (good) {
				auto output = QFile(outputPath);
				good = output.open(QIODevice::WriteOnly);
				auto buffer = QByteArray(1024 * 1024, Qt::Uninitialized);
				while (good) {
					const auto read = unzReadCurrentFile(
						archive,
						buffer.data(),
						unsigned(buffer.size()));
					if (read < 0) {
						good = false;
						break;
					} else if (!read) {
						break;
					} else if (output.write(buffer.constData(), read) != read) {
						good = false;
						break;
					}
				}
				good = (unzCloseCurrentFile(archive) == UNZ_OK) && good;
			}
		}
		if (!good) {
			break;
		}
		result = unzGoToNextFile(archive);
		if (result == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (result != UNZ_OK) {
			good = false;
			break;
		}
	}
	unzClose(archive);
	return good;
}
#elif defined Q_OS_MAC // Q_OS_WIN
bool ExtractZip(const QString &filepath, const QString &destination) {
	return ValidateZip(filepath)
		&& RunProcess(u"/usr/bin/ditto"_q, {
			u"-x"_q,
			u"-k"_q,
			filepath,
			destination,
		});
}
#else // Q_OS_WIN || Q_OS_MAC
bool ExtractTar(const QString &filepath, const QString &destination) {
	const auto tar = QStandardPaths::findExecutable(u"tar"_q);
	if (tar.isEmpty()) {
		return false;
	}
	auto listing = QByteArray();
	auto verbose = QByteArray();
	return RunProcess(tar, { u"-tzf"_q, filepath }, &listing)
		&& ValidateMemberList(listing)
		&& RunProcess(tar, {
			u"-tvzf"_q,
			filepath,
			u"--numeric-owner"_q,
		}, &verbose)
		&& ValidateTarSize(verbose)
		&& RunProcess(tar, {
			u"-xzf"_q,
			filepath,
			u"-C"_q,
			destination,
			u"--no-same-owner"_q,
		});
}
#endif // else for Q_OS_WIN || Q_OS_MAC

bool ValidExtractedTree(const QString &destination) {
	auto size = int64(0);
	auto iterator = QDirIterator(
		destination,
		QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
		QDirIterator::Subdirectories);
	const auto prefix = QDir(destination).absolutePath() + '/';
	while (iterator.hasNext()) {
		iterator.next();
		const auto info = iterator.fileInfo();
		if (info.isSymLink()) {
#ifdef Q_OS_MAC
			const auto target = QDir::cleanPath(info.symLinkTarget());
			if (!target.startsWith(prefix)) {
				return false;
			}
#else // Q_OS_MAC
			return false;
#endif // else of Q_OS_MAC
		} else if (!info.isFile() && !info.isDir()) {
			return false;
		}
		if (info.isFile()) {
			size += info.size();
			if (size > UpdateMetadata::kMaximumExpandedSize) {
				return false;
			}
		}
	}
	return true;
}

bool ValidLayout(const QString &destination) {
	const auto entries = QDir(destination).entryInfoList(
		QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
#ifdef Q_OS_WIN
	for (const auto &entry : entries) {
		if (entry.isDir() && entry.fileName() != u"modules"_q) {
			return false;
		}
	}
	return QFileInfo(destination + u"/AywGram.exe"_q).isFile()
		&& QFileInfo(destination + u"/Updater.exe"_q).isFile();
#elif defined Q_OS_MAC // Q_OS_WIN
	const auto application = destination + u"/AywGram.app/Contents/MacOS/AywGram"_q;
	const auto updater = destination
		+ u"/AywGram.app/Contents/Frameworks/Updater"_q;
	const auto applicationInfo = QFileInfo(application);
	const auto updaterInfo = QFileInfo(updater);
	return entries.size() == 1
		&& entries.front().fileName() == u"AywGram.app"_q
		&& QFileInfo(destination + u"/AywGram.app"_q).isDir()
		&& applicationInfo.isFile()
		&& !applicationInfo.isSymLink()
		&& applicationInfo.isExecutable()
		&& updaterInfo.isFile()
		&& !updaterInfo.isSymLink()
		&& updaterInfo.isExecutable();
#else // Q_OS_WIN || Q_OS_MAC
	const auto application = QFileInfo(destination + u"/AywGram"_q);
	const auto updater = QFileInfo(destination + u"/Updater"_q);
	return entries.size() == 2
		&& application.isFile()
		&& application.isExecutable()
		&& updater.isFile()
		&& updater.isExecutable();
#endif // else for Q_OS_WIN || Q_OS_MAC
}

bool WriteVersion(const QString &destination, const UpdateMetadata::Asset &asset) {
	const auto tdata = destination + u"/tdata"_q;
	if (!QDir().mkpath(tdata)) {
		return false;
	}
	const auto versionString = asset.versionName.toStdWString();
	const auto versionNum = VersionInt(asset.appVersion);
	const auto versionLength = VersionInt(
		versionString.size() * sizeof(VersionChar));
	auto output = QFile(tdata + u"/version"_q);
	if (!output.open(QIODevice::WriteOnly)
		|| output.write(
			reinterpret_cast<const char*>(&versionNum),
			sizeof(versionNum)) != sizeof(versionNum)
		|| output.write(
			reinterpret_cast<const char*>(&versionLength),
			sizeof(versionLength)) != sizeof(versionLength)
		|| output.write(
			reinterpret_cast<const char*>(versionString.data()),
			versionLength) != versionLength) {
		return false;
	}
	return true;
}

bool WriteManifest(const QString &destination, const UpdateMetadata::Asset &asset) {
	const auto document = QJsonDocument(QJsonObject{
		{ u"schema"_q, kStagingSchema },
		{ u"target"_q, UpdateMetadata::CurrentTarget() },
		{ u"app_version"_q, asset.appVersion },
		{ u"revision"_q, asset.revision },
		{ u"version_name"_q, asset.versionName },
		{ u"release"_q, asset.release },
		{ u"format"_q, asset.format },
		{ u"size"_q, double(asset.size) },
		{ u"sha256"_q, QString::fromLatin1(asset.sha256) },
	});
	auto output = QFile(destination + u"/update-metadata.json"_q);
	return output.open(QIODevice::WriteOnly)
		&& output.write(document.toJson(QJsonDocument::Compact)) > 0;
}

} // namespace

bool UnpackReleaseUpdate(
		const QString &filepath,
		const UpdateMetadata::Asset &asset) {
#ifdef TDESKTOP_DISABLE_AUTOUPDATE
	return false;
#else // TDESKTOP_DISABLE_AUTOUPDATE
	const auto destination = TempFolder();
	base::Platform::DeleteDirectory(destination);
	if (!VerifyArchive(filepath, asset)
		|| !QDir().mkpath(destination)) {
		base::Platform::DeleteDirectory(destination);
		return false;
	}
#if defined Q_OS_WIN || defined Q_OS_MAC
	const auto extracted = (asset.format == u"zip"_q)
		&& ExtractZip(filepath, destination);
#else // Q_OS_WIN || Q_OS_MAC
	const auto extracted = (asset.format == u"tar.gz"_q)
		&& ExtractTar(filepath, destination);
#endif // else for Q_OS_WIN || Q_OS_MAC
	if (!extracted
		|| !ValidExtractedTree(destination)
		|| !ValidLayout(destination)
		|| !WriteVersion(destination, asset)
		|| !WriteManifest(destination, asset)) {
		base::Platform::DeleteDirectory(destination);
		return false;
	}
	auto ready = QFile(destination + u"/ready"_q);
	if (!ready.open(QIODevice::WriteOnly) || ready.write("1", 1) != 1) {
		base::Platform::DeleteDirectory(destination);
		return false;
	}
	QFile::remove(filepath);
	return true;
#endif // else of TDESKTOP_DISABLE_AUTOUPDATE
}

} // namespace Core
