#include "ayu/cloud/ayu_cloud_codec.h"

#include "base/random.h"
#include "base/qt/qt_common_adapters.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <zlib.h>

#include <algorithm>

namespace AyuCloud {
namespace {

Error Failure(ErrorCode code, QString details = {}) {
	return {
		.code = code,
		.details = std::move(details),
	};
}

bool IsLowerHex(const QString &value, int size) {
	if (value.size() != size) {
		return false;
	}
	return std::all_of(value.begin(), value.end(), [](QChar ch) {
		return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
	});
}

base::expected<QByteArray, Error> Gzip(const QByteArray &data) {
	auto stream = z_stream{};
	if (deflateInit2(
			&stream,
			Z_DEFAULT_COMPRESSION,
			Z_DEFLATED,
			MAX_WBITS + 16,
			8,
			Z_DEFAULT_STRATEGY) != Z_OK) {
		return base::unexpected(Failure(ErrorCode::CompressionFailed));
	}

	stream.next_in = reinterpret_cast<Bytef*>(
		const_cast<char*>(data.constData()));
	stream.avail_in = static_cast<uInt>(data.size());
	auto result = QByteArray();
	auto buffer = QByteArray(16 * 1024, Qt::Uninitialized);
	auto code = Z_OK;
	while (code == Z_OK) {
		stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
		stream.avail_out = static_cast<uInt>(buffer.size());
		code = deflate(&stream, Z_FINISH);
		result.append(buffer.constData(), buffer.size() - stream.avail_out);
	}
	deflateEnd(&stream);
	if (code != Z_STREAM_END) {
		return base::unexpected(Failure(
			ErrorCode::CompressionFailed,
			QString::number(code)));
	}
	return result;
}

base::expected<QByteArray, Error> Gunzip(const QByteArray &data) {
	auto stream = z_stream{};
	if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) {
		return base::unexpected(Failure(ErrorCode::DecompressionFailed));
	}
	stream.next_in = reinterpret_cast<Bytef*>(
		const_cast<char*>(data.constData()));
	stream.avail_in = static_cast<uInt>(data.size());
	auto result = QByteArray();
	auto buffer = QByteArray(16 * 1024, Qt::Uninitialized);
	auto code = Z_OK;
	while (code == Z_OK) {
		stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
		stream.avail_out = static_cast<uInt>(buffer.size());
		code = inflate(&stream, Z_NO_FLUSH);
		result.append(buffer.constData(), buffer.size() - stream.avail_out);
		if (result.size() > kMaximumPayloadSize) {
			inflateEnd(&stream);
			return base::unexpected(Failure(ErrorCode::PayloadTooLarge));
		}
	}
	inflateEnd(&stream);
	if (code != Z_STREAM_END) {
		return base::unexpected(Failure(
			ErrorCode::DecompressionFailed,
			QString::number(code)));
	}
	return result;
}

QJsonObject SerializeGeneration(const Generation &generation) {
	if (generation.id.isEmpty()) {
		return {};
	}
	return {
		{ u"id"_q, generation.id },
		{ u"parts"_q, generation.parts },
		{ u"sha256"_q, generation.sha256 },
	};
}

base::expected<Generation, Error> ParseGeneration(
		const QJsonValue &value,
		bool required) {
	if (!value.isObject()) {
		if (required) {
			return base::unexpected(Failure(ErrorCode::InvalidManifest));
		}
		return Generation();
	}
	const auto object = value.toObject();
	if (!required && object.isEmpty()) {
		return Generation();
	}
	const auto result = Generation{
		.id = object.value(u"id"_q).toString(),
		.parts = object.value(u"parts"_q).toInt(),
		.sha256 = object.value(u"sha256"_q).toString(),
	};
	if (!IsLowerHex(result.id, 32)
		|| result.parts <= 0
		|| result.parts > kMaximumChunks
		|| !IsLowerHex(result.sha256, 64)) {
		return base::unexpected(Failure(ErrorCode::InvalidManifest));
	}
	return result;
}

} // namespace

base::expected<EncodedPayload, Error> EncodePayload(
		const QByteArray &canonical) {
	if (canonical.size() > kMaximumPayloadSize) {
		return base::unexpected(Failure(ErrorCode::PayloadTooLarge));
	}
	const auto compressed = Gzip(canonical);
	if (!compressed) {
		return base::unexpected(compressed.error());
	}
	const auto encoded = compressed->toBase64();
	const auto count = (encoded.size() + kChunkSize - 1) / kChunkSize;
	if (count <= 0 || count > kMaximumChunks) {
		return base::unexpected(Failure(ErrorCode::TooManyChunks));
	}
	auto chunks = std::vector<QString>();
	chunks.reserve(count);
	for (auto offset = 0; offset < encoded.size(); offset += kChunkSize) {
		chunks.push_back(QString::fromLatin1(encoded.mid(offset, kChunkSize)));
	}
	return EncodedPayload{
		.canonical = canonical,
		.compressed = *compressed,
		.sha256 = Sha256(canonical),
		.chunks = std::move(chunks),
	};
}

base::expected<QByteArray, Error> DecodePayload(
		const std::vector<QString> &chunks,
		const QString &sha256) {
	if (chunks.empty() || chunks.size() > kMaximumChunks) {
		return base::unexpected(Failure(ErrorCode::TooManyChunks));
	}
	auto encoded = QByteArray();
	for (const auto &chunk : chunks) {
		if (chunk.isEmpty() || chunk.size() > kChunkSize) {
			return base::unexpected(Failure(ErrorCode::MissingChunk));
		}
		encoded.append(chunk.toLatin1());
	}
	const auto decoded = QByteArray::fromBase64Encoding(
		std::move(encoded),
		QByteArray::Base64Encoding
			| QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded) {
		return base::unexpected(Failure(ErrorCode::InvalidBase64));
	}
	const auto canonical = Gunzip(*decoded);
	if (!canonical) {
		return base::unexpected(canonical.error());
	}
	if (Sha256(*canonical) != sha256.toLower()) {
		return base::unexpected(Failure(ErrorCode::HashMismatch));
	}
	return *canonical;
}

QByteArray SerializeManifest(const Manifest &manifest) {
	const auto object = QJsonObject{
		{ u"schema"_q, manifest.schema },
		{ u"revision"_q, QString::number(manifest.revision) },
		{ u"base_revision"_q, QString::number(manifest.baseRevision) },
		{ u"current"_q, SerializeGeneration(manifest.current) },
		{ u"previous"_q, SerializeGeneration(manifest.previous) },
		{ u"settings_sha256"_q, manifest.settingsHash },
		{ u"updated_at"_q, QString::number(manifest.updatedAt) },
		{ u"device_id"_q, manifest.deviceId },
		{ u"client_version"_q, manifest.clientVersion },
		{ u"account_id"_q, QString::number(manifest.accountId) },
		{ u"categories"_q, int(manifest.categories) },
		{ u"encoding"_q, u"gzip+base64"_q },
	};
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

base::expected<Manifest, Error> ParseManifest(const QByteArray &data) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return base::unexpected(Failure(
			ErrorCode::InvalidManifest,
			error.errorString()));
	}
	const auto object = document.object();
	if (object.value(u"schema"_q).toInt() != 1
		|| object.value(u"encoding"_q).toString() != u"gzip+base64"_q) {
		return base::unexpected(Failure(ErrorCode::UnsupportedSchema));
	}
	const auto current = ParseGeneration(object.value(u"current"_q), true);
	if (!current) {
		return base::unexpected(current.error());
	}
	const auto previous = ParseGeneration(object.value(u"previous"_q), false);
	if (!previous) {
		return base::unexpected(previous.error());
	}
	auto valid = false;
	const auto revision = object.value(u"revision"_q).toString().toULongLong(&valid);
	if (!valid || !revision) {
		return base::unexpected(Failure(ErrorCode::InvalidManifest));
	}
	const auto baseRevision = object.value(
		u"base_revision"_q).toString().toULongLong(&valid);
	if (!valid) {
		return base::unexpected(Failure(ErrorCode::InvalidManifest));
	}
	const auto updatedAt = object.value(
		u"updated_at"_q).toString().toULongLong(&valid);
	if (!valid) {
		return base::unexpected(Failure(ErrorCode::InvalidManifest));
	}
	const auto accountId = object.value(
		u"account_id"_q).toString().toULongLong(&valid);
	if (!valid) {
		return base::unexpected(Failure(ErrorCode::InvalidManifest));
	}
	const auto categories = uint32_t(object.value(u"categories"_q).toInt());
	const auto deviceId = object.value(u"device_id"_q).toString();
	const auto settingsHash = object.value(u"settings_sha256"_q).toString();
	if (!accountId
		|| !categories
		|| (categories & ~uint32_t(0x0F))
		|| baseRevision + 1 != revision
		|| !IsLowerHex(deviceId, 32)
		|| !IsLowerHex(settingsHash, 64)) {
		return base::unexpected(Failure(ErrorCode::InvalidManifest));
	}
	return Manifest{
		.schema = 1,
		.revision = revision,
		.baseRevision = baseRevision,
		.current = *current,
		.previous = *previous,
		.settingsHash = settingsHash,
		.updatedAt = updatedAt,
		.deviceId = deviceId,
		.clientVersion = object.value(u"client_version"_q).toString(),
		.accountId = accountId,
		.categories = categories,
	};
}

QString ChunkKey(const QString &generation, int index) {
	return QString::fromLatin1(kChunkPrefix)
		+ generation
		+ u"_"_q
		+ QString::number(index);
}

QString RandomId() {
	auto data = QByteArray(16, Qt::Uninitialized);
	base::RandomFill(data.data(), data.size());
	return QString::fromLatin1(data.toHex());
}

QString Sha256(const QByteArray &data) {
	return QString::fromLatin1(QCryptographicHash::hash(
		data,
		QCryptographicHash::Sha256).toHex());
}

} // namespace AyuCloud
