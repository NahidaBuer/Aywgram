#include "ayu/cloud/ayu_cloud_codec.h"

#include "base/qt/qt_common_adapters.h"

#include <QtCore/QCoreApplication>

#include <iostream>
#include <algorithm>

#include <zlib.h>

namespace {

using namespace AyuCloud;

bool Check(bool condition, const char *message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}

QByteArray GzipForTest(const QByteArray &data) {
	auto stream = z_stream{};
	if (deflateInit2(
		&stream,
		Z_DEFAULT_COMPRESSION,
		Z_DEFLATED,
		MAX_WBITS + 16,
		8,
		Z_DEFAULT_STRATEGY) != Z_OK) {
		return {};
	}
	stream.next_in = reinterpret_cast<Bytef*>(
		const_cast<char*>(data.constData()));
	stream.avail_in = uInt(data.size());
	auto result = QByteArray();
	auto buffer = QByteArray(16 * 1024, Qt::Uninitialized);
	auto code = Z_OK;
	while (code == Z_OK) {
		stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
		stream.avail_out = uInt(buffer.size());
		code = deflate(&stream, Z_FINISH);
		result.append(buffer.constData(), buffer.size() - stream.avail_out);
	}
	deflateEnd(&stream);
	return (code == Z_STREAM_END) ? result : QByteArray();
}

std::vector<QString> ChunksForCompressed(const QByteArray &compressed) {
	const auto encoded = compressed.toBase64();
	auto result = std::vector<QString>();
	for (auto offset = 0; offset < encoded.size(); offset += kChunkSize) {
		result.push_back(QString::fromLatin1(encoded.mid(offset, kChunkSize)));
	}
	return result;
}

bool RunTests() {
	auto good = true;
	auto input = QByteArray();
	input.reserve(250000);
	for (auto i = 0; i != 250000; ++i) {
		input.push_back(char((i * 31) % 251));
	}
	const auto encoded = EncodePayload(input);
	good &= Check(bool(encoded), "encode payload");
	if (encoded) {
		good &= Check(!encoded->chunks.empty(), "payload chunks");
		good &= Check(std::all_of(
			encoded->chunks.begin(),
			encoded->chunks.end(),
			[](const QString &part) {
			return part.size() <= kChunkSize;
		}), "chunk size limit");
		const auto decoded = DecodePayload(encoded->chunks, encoded->sha256);
		good &= Check(decoded && *decoded == input, "payload round trip");
		auto corrupt = encoded->chunks;
		corrupt.front()[0] = (corrupt.front()[0] == 'A') ? 'B' : 'A';
		good &= Check(!DecodePayload(corrupt, encoded->sha256), "corrupt payload");
		good &= Check(!DecodePayload(encoded->chunks, QString(64, '0')), "wrong hash");
	}

	const auto tooLarge = QByteArray(kMaximumPayloadSize + 1, 'x');
	good &= Check(!EncodePayload(tooLarge), "uncompressed size limit");
	good &= Check(!DecodePayload({}, QString(64, '0')), "missing chunks");
	good &= Check(!DecodePayload({ QString() }, QString(64, '0')),
		"empty chunk");
	good &= Check(!DecodePayload({ u"%%%"_q }, QString(64, '0')),
		"invalid base64");
	good &= Check(!DecodePayload(
		std::vector<QString>(kMaximumChunks + 1, u"A"_q),
		QString(64, '0')), "chunk count limit");
	const auto bombCompressed = GzipForTest(tooLarge);
	good &= Check(!bombCompressed.isEmpty(), "prepare zip bomb");
	good &= Check(!DecodePayload(
		ChunksForCompressed(bombCompressed),
		Sha256(tooLarge)), "decompressed size limit");
	const auto boundary = QByteArray(kMaximumPayloadSize, 'y');
	const auto boundaryDecoded = DecodePayload(
		ChunksForCompressed(GzipForTest(boundary)),
		Sha256(boundary));
	good &= Check(boundaryDecoded && boundaryDecoded->size() == boundary.size(),
		"maximum decompressed size");

	const auto manifest = Manifest{
		.revision = 42,
		.baseRevision = 41,
		.current = { QString(32, 'a'), 3, QString(64, 'b') },
		.previous = { QString(32, 'c'), 2, QString(64, 'd') },
		.settingsHash = QString(64, 'e'),
		.updatedAt = 123456789,
		.deviceId = QString(32, 'f'),
		.clientVersion = u"7.1.3"_q,
		.accountId = 777000,
		.categories = uint32_t(0x0F),
	};
	const auto parsed = ParseManifest(SerializeManifest(manifest));
	good &= Check(bool(parsed), "manifest round trip");
	if (parsed) {
		good &= Check(parsed->revision == manifest.revision, "manifest revision");
		good &= Check(parsed->current.id == manifest.current.id, "manifest generation");
		good &= Check(parsed->previous.id == manifest.previous.id, "previous generation");
		good &= Check(parsed->accountId == manifest.accountId, "manifest account");
	}
	good &= Check(!ParseManifest("{}"), "invalid manifest");
	auto invalidRevision = manifest;
	invalidRevision.baseRevision = invalidRevision.revision;
	good &= Check(!ParseManifest(SerializeManifest(invalidRevision)),
		"invalid base revision");
	auto invalidHash = manifest;
	invalidHash.settingsHash = QString(64, 'z');
	good &= Check(!ParseManifest(SerializeManifest(invalidHash)),
		"invalid settings hash");
	auto first = manifest;
	first.revision = 1;
	first.baseRevision = 0;
	first.previous = {};
	good &= Check(bool(ParseManifest(SerializeManifest(first))),
		"first manifest without previous generation");
	good &= Check(ChunkKey(QString(32, 'f'), 12)
		== u"ayw_sync_ffffffffffffffffffffffffffffffff_12"_q,
		"chunk key");
	good &= Check(RandomId().size() == 32, "random generation id");
	return good;
}

} // namespace

int main(int argc, char *argv[]) {
	const auto application = QCoreApplication(argc, argv);
	return RunTests() ? 0 : 1;
}
