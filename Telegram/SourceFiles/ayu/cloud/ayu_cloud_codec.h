#pragma once

#include "base/expected.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <cstdint>
#include <vector>

namespace AyuCloud {

inline constexpr auto kManifestKey = "ayw_sync_manifest_v1";
inline constexpr auto kChunkPrefix = "ayw_sync_";
inline constexpr auto kChunkSize = 3500;
inline constexpr auto kMaximumChunks = 256;
inline constexpr auto kMaximumPayloadSize = 4 * 1024 * 1024;

enum class ErrorCode {
	InvalidJson,
	InvalidManifest,
	InvalidBase64,
	CompressionFailed,
	DecompressionFailed,
	PayloadTooLarge,
	TooManyChunks,
	MissingChunk,
	HashMismatch,
	UnsupportedSchema,
	InvalidStorageResponse,
	BotUnavailable,
	Network,
	Conflict,
};

struct Error {
	ErrorCode code = ErrorCode::InvalidJson;
	QString details;
};

struct Generation {
	QString id;
	int parts = 0;
	QString sha256;
};

struct Manifest {
	int schema = 1;
	uint64_t revision = 0;
	uint64_t baseRevision = 0;
	Generation current;
	Generation previous;
	QString settingsHash;
	uint64_t updatedAt = 0;
	QString deviceId;
	QString clientVersion;
	uint64_t accountId = 0;
	uint32_t categories = 0;
};

struct EncodedPayload {
	QByteArray canonical;
	QByteArray compressed;
	QString sha256;
	std::vector<QString> chunks;
};

[[nodiscard]] base::expected<EncodedPayload, Error> EncodePayload(
	const QByteArray &canonical);
[[nodiscard]] base::expected<QByteArray, Error> DecodePayload(
	const std::vector<QString> &chunks,
	const QString &sha256);

[[nodiscard]] QByteArray SerializeManifest(const Manifest &manifest);
[[nodiscard]] base::expected<Manifest, Error> ParseManifest(
	const QByteArray &data);
[[nodiscard]] QString ChunkKey(const QString &generation, int index);
[[nodiscard]] QString RandomId();
[[nodiscard]] QString Sha256(const QByteArray &data);

} // namespace AyuCloud
