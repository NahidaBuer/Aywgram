/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ayu/session_transfer/session_transfer_codec.h"

#include "base/qt/qt_common_adapters.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <algorithm>
#include <iostream>

namespace {

using namespace Ayu::SessionTransfer;

constexpr auto kVectorOne =
	"AgAAB_gACxIZICcuNTxDSlFYX2ZtdHuCiZCXnqWss7rByM_W3eTr8vkABw4VHCMqMTg_Rk1UW2JpcHd-hYyTmqGor7a9xMvS2eDn7vX8AwoRGB8mLTQ7QklQV15lbHN6gYiPlp2kq7K5wMfO1dzj6vH4_wYNFBsiKTA3PkVMU1phaG92fYSLkpmgp661vMPK0djf5u30-wIJEBceJSwzOkFIT1ZdZGtyeYCHjpWco6qxuL_GzdTb4unw9_4FDBMaISgvNj1ES1JZYGdudXyDipGYn6attLvCydDX3uXs8_oBCA8WHSQrMjlAR05VXGNqcXh_ho2Um6KpsLe-xczT2uHo7_b9BAAAAAAAC9soAA";
constexpr auto kVectorTwo =
	"BQAJVAcB__79_Pv6-fj39vX08_Lx8O_u7ezr6uno5-bl5OPi4eDf3t3c29rZ2NfW1dTT0tHQz87NzMvKycjHxsXEw8LBwL--vby7urm4t7a1tLOysbCvrq2sq6qpqKempaSjoqGgn56dnJuamZiXlpWUk5KRkI-OjYyLiomIh4aFhIOCgYB_fn18e3p5eHd2dXRzcnFwb25tbGtqaWhnZmVkY2JhYF9eXVxbWllYV1ZVVFNSUVBPTk1MS0pJSEdGRURDQkFAPz49PDs6OTg3NjU0MzIxMC8uLSwrKikoJyYlJCMiISAfHh0cGxoZGBcWFRQTEhEQDw4NDAsKCQgHBgUEAwIBAAAAAAGhO4YAAQ";

[[nodiscard]] QByteArray KeyOne() {
	auto result = QByteArray(256, Qt::Uninitialized);
	for (auto i = 0; i != result.size(); ++i) {
		result[i] = char((i * 7 + 11) % 256);
	}
	return result;
}

[[nodiscard]] QByteArray KeyTwo() {
	auto result = QByteArray(256, Qt::Uninitialized);
	for (auto i = 0; i != result.size(); ++i) {
		result[i] = char(255 - i);
	}
	return result;
}

[[nodiscard]] bool Check(bool condition, const char *message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}

[[nodiscard]] bool RunTests() {
	auto good = true;
	const auto one = DecodeSessionString(QString::fromLatin1(kVectorOne));
	good &= Check(bool(one), "decode vector one");
	if (one) {
		good &= Check(one->dcId == 2, "vector one dc");
		good &= Check(one->apiId == 2040, "vector one api id");
		good &= Check(!one->testMode, "vector one production flag");
		good &= Check(one->authKey == KeyOne(), "vector one auth key");
		good &= Check(one->userId == 777000, "vector one user id");
		good &= Check(!one->bot, "vector one bot flag");
		const auto encoded = EncodeSessionString(*one);
		good &= Check(encoded && *encoded == QString::fromLatin1(kVectorOne),
			"encode vector one");
		good &= Check(encoded && encoded->size() == 362,
			"canonical size");
	}

	const auto two = DecodeSessionString(QString::fromLatin1(kVectorTwo));
	good &= Check(bool(two), "decode vector two");
	if (two) {
		good &= Check(two->dcId == 5, "vector two dc");
		good &= Check(two->apiId == 611335, "vector two api id");
		good &= Check(two->testMode, "vector two test flag");
		good &= Check(two->authKey == KeyTwo(), "vector two auth key");
		good &= Check(two->userId == 7000000000ULL,
			"vector two large user id");
		good &= Check(two->bot, "vector two bot flag");
	}

	auto tolerant = QString::fromLatin1(kVectorOne);
	tolerant.replace('-', '+').replace('_', '/');
	tolerant = u" \n"_q + tolerant + u"==\t"_q;
	good &= Check(bool(DecodeSessionString(tolerant)),
		"standard alphabet, padding and whitespace");
	good &= Check(!DecodeSessionString(u"not base64"_q), "invalid base64");
	good &= Check(!DecodeSessionString(u"AQ"_q), "invalid payload size");
	auto raw = QByteArray::fromBase64(
		QByteArray(kVectorOne),
		QByteArray::Base64UrlEncoding);
	raw[5] = 2;
	good &= Check(!DecodeSessionString(qs(raw.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))),
		"invalid test-mode byte");
	raw = QByteArray::fromBase64(
		QByteArray(kVectorOne),
		QByteArray::Base64UrlEncoding);
	raw[270] = 2;
	good &= Check(!DecodeSessionString(qs(raw.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))),
		"invalid bot byte");
	raw = QByteArray::fromBase64(
		QByteArray(kVectorOne),
		QByteArray::Base64UrlEncoding);
	std::fill(raw.begin() + 6, raw.begin() + 262, 0);
	good &= Check(!DecodeSessionString(qs(raw.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))),
		"zero auth key payload");

	if (one) {
		auto invalid = *one;
		invalid.authKey.fill(0);
		good &= Check(!EncodeSessionString(invalid), "zero auth key");
		invalid = *one;
		invalid.dcId = 0;
		good &= Check(!EncodeSessionString(invalid), "zero dc");
		invalid = *one;
		invalid.userId = 0;
		good &= Check(!EncodeSessionString(invalid), "zero user id");
	}

	auto envelope = Envelope();
	envelope.id = u"test"_q;
	envelope.accountId = u"777000"_q;
	envelope.userId = 777000;
	envelope.name = u"Test User"_q;
	envelope.storage = u"local"_q;
	envelope.createdAt = QDateTime::fromString(
		u"2026-08-20T11:22:33.444Z"_q,
		Qt::ISODateWithMs);
	envelope.sessionString = QString::fromLatin1(kVectorOne);
	const auto json = EncodeEnvelope(envelope);
	good &= Check(bool(json), "encode envelope");
	if (json) {
		good &= Check(json->contains(kEnvelopeFormat), "v2 format output");
		const auto decoded = DecodeEnvelope(*json);
		good &= Check(decoded && decoded->userId == 777000,
			"decode v2 envelope");
		auto legacy = *json;
		legacy.replace(kEnvelopeFormat, kLegacyEnvelopeFormat);
		good &= Check(bool(DecodeEnvelope(legacy)), "decode v1 envelope");
		auto unknown = *json;
		unknown.replace(kEnvelopeFormat, "unknown.format");
		good &= Check(!DecodeEnvelope(unknown), "reject unknown envelope");
		auto mismatch = *json;
		mismatch.replace("\"userId\": 777000", "\"userId\": 777001");
		good &= Check(!DecodeEnvelope(mismatch), "reject user mismatch");
	}
	return good;
}

int DecodeCommand(const QString &value) {
	const auto decoded = DecodeSessionString(value);
	if (!decoded) {
		return 1;
	}
	auto object = QJsonObject();
	object.insert(u"dcId"_q, decoded->dcId);
	object.insert(u"apiId"_q, int(decoded->apiId));
	object.insert(u"testMode"_q, decoded->testMode);
	object.insert(u"authKey"_q, QString::fromLatin1(decoded->authKey.toHex()));
	object.insert(u"userId"_q, QString::number(decoded->userId));
	object.insert(u"bot"_q, decoded->bot);
	std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).constData()
		<< '\n';
	return 0;
}

int EncodeCommand(const QStringList &arguments) {
	if (arguments.size() != 8) {
		return 2;
	}
	auto ok = false;
	auto data = SessionData();
	data.dcId = arguments[2].toInt(&ok);
	if (!ok) return 2;
	data.apiId = arguments[3].toUInt(&ok);
	if (!ok) return 2;
	data.testMode = (arguments[4] == u"1"_q);
	data.authKey = QByteArray::fromHex(arguments[5].toLatin1());
	data.userId = arguments[6].toULongLong(&ok);
	if (!ok) return 2;
	data.bot = (arguments[7] == u"1"_q);
	const auto encoded = EncodeSessionString(data);
	if (!encoded) {
		return 1;
	}
	std::cout << encoded->toStdString() << '\n';
	return 0;
}

} // namespace

int main(int argc, char *argv[]) {
	const auto application = QCoreApplication(argc, argv);
	const auto arguments = application.arguments();
	if (arguments.size() == 3 && arguments[1] == u"--decode"_q) {
		return DecodeCommand(arguments[2]);
	} else if (arguments.size() >= 2 && arguments[1] == u"--encode"_q) {
		return EncodeCommand(arguments);
	}
	return RunTests() ? 0 : 1;
}
