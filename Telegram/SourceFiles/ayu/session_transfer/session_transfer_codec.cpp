/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ayu/session_transfer/session_transfer_codec.h"

#include "base/qt/qt_common_adapters.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimeZone>
#include <QtCore/QtEndian>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Ayu::SessionTransfer {
namespace {

constexpr auto kRawSize = 271;
constexpr auto kAuthKeySize = 256;
constexpr auto kAuthKeyOffset = 6;
constexpr auto kUserIdOffset = kAuthKeyOffset + kAuthKeySize;
constexpr auto kBotOffset = kUserIdOffset + int(sizeof(uint64));
constexpr auto kMaximumExactJsonInteger = uint64(1) << 53;

[[nodiscard]] base::unexpected<Error> Failure(
		ErrorCode code,
		QString detail = {}) {
	return base::make_unexpected(Error{ code, std::move(detail) });
}

[[nodiscard]] bool HasNonZeroByte(const QByteArray &value) {
	return std::any_of(
		value.cbegin(),
		value.cend(),
		[](char byte) { return byte != 0; });
}

[[nodiscard]] base::expected<uint64, Error> ReadJsonInteger(
		const QJsonValue &value,
		ErrorCode code,
		const QString &field) {
	if (value.isString()) {
		auto ok = false;
		const auto result = value.toString().toULongLong(&ok);
		if (ok) {
			return result;
		}
	} else if (value.isDouble()) {
		const auto number = value.toDouble();
		if (std::isfinite(number)
			&& number >= 0.
			&& number <= double(kMaximumExactJsonInteger)
			&& std::floor(number) == number) {
			return uint64(number);
		}
	}
	return Failure(code, field);
}

[[nodiscard]] bool IsStringOrNull(const QJsonValue &value) {
	return value.isUndefined() || value.isString() || value.isNull();
}

[[nodiscard]] QString ReadJsonString(const QJsonValue &value) {
	if (value.isString()) {
		return value.toString();
	} else if (value.isDouble()) {
		const auto number = value.toDouble();
		if (std::isfinite(number)
			&& number >= 0.
			&& number <= double(kMaximumExactJsonInteger)
			&& std::floor(number) == number) {
			return QString::number(uint64(number));
		}
	}
	return QString();
}

} // namespace

base::expected<SessionData, Error> DecodeSessionString(QStringView value) {
	auto encoded = QByteArray();
	encoded.reserve(value.size() + 3);
	for (const auto character : value) {
		if (character.isSpace()) {
			continue;
		} else if (character.unicode() > 0x7F) {
			return Failure(ErrorCode::InvalidBase64);
		}
		const auto latin = char(character.unicode());
		encoded.push_back((latin == '-') ? '+' : (latin == '_') ? '/' : latin);
	}
	while ((encoded.size() % 4) != 0) {
		encoded.push_back('=');
	}
	const auto decoded = QByteArray::fromBase64Encoding(
		std::move(encoded),
		QByteArray::Base64Encoding
			| QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded) {
		return Failure(ErrorCode::InvalidBase64);
	}
	const auto &raw = *decoded;
	if (raw.size() != kRawSize) {
		return Failure(
			ErrorCode::InvalidSize,
			QString::number(raw.size()));
	}

	auto result = SessionData();
	result.dcId = uchar(raw[0]);
	if (!result.dcId) {
		return Failure(ErrorCode::InvalidDcId);
	}
	result.apiId = qFromBigEndian<uint32>(
		reinterpret_cast<const uchar*>(raw.constData() + 1));
	if (!result.apiId
		|| result.apiId > uint32(std::numeric_limits<int32>::max())) {
		return Failure(ErrorCode::InvalidApiId);
	}
	const auto testMode = uchar(raw[5]);
	if (testMode > 1) {
		return Failure(ErrorCode::InvalidTestMode);
	}
	result.testMode = testMode;
	result.authKey = raw.mid(kAuthKeyOffset, kAuthKeySize);
	if (!HasNonZeroByte(result.authKey)) {
		return Failure(ErrorCode::InvalidAuthKey);
	}
	result.userId = qFromBigEndian<uint64>(
		reinterpret_cast<const uchar*>(raw.constData() + kUserIdOffset));
	if (!result.userId
		|| result.userId > uint64(std::numeric_limits<int64>::max())) {
		return Failure(ErrorCode::InvalidUserId);
	}
	const auto bot = uchar(raw[kBotOffset]);
	if (bot > 1) {
		return Failure(ErrorCode::InvalidBotFlag);
	}
	result.bot = bot;
	return result;
}

base::expected<QString, Error> EncodeSessionString(
		const SessionData &data) {
	if (data.dcId <= 0 || data.dcId > 255) {
		return Failure(ErrorCode::InvalidDcId);
	} else if (!data.apiId
		|| data.apiId > uint32(std::numeric_limits<int32>::max())) {
		return Failure(ErrorCode::InvalidApiId);
	} else if (data.authKey.size() != kAuthKeySize
		|| !HasNonZeroByte(data.authKey)) {
		return Failure(ErrorCode::InvalidAuthKey);
	} else if (!data.userId
		|| data.userId > uint64(std::numeric_limits<int64>::max())) {
		return Failure(ErrorCode::InvalidUserId);
	}

	auto raw = QByteArray(kRawSize, Qt::Uninitialized);
	raw[0] = char(data.dcId);
	qToBigEndian<uint32>(
		data.apiId,
		reinterpret_cast<uchar*>(raw.data() + 1));
	raw[5] = data.testMode ? 1 : 0;
	std::copy(
		data.authKey.cbegin(),
		data.authKey.cend(),
		raw.begin() + kAuthKeyOffset);
	qToBigEndian<uint64>(
		data.userId,
		reinterpret_cast<uchar*>(raw.data() + kUserIdOffset));
	raw[kBotOffset] = data.bot ? 1 : 0;
	return qs(raw.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

base::expected<Envelope, Error> DecodeEnvelope(const QByteArray &json) {
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError
		|| !document.isObject()) {
		return Failure(ErrorCode::InvalidJson);
	}
	const auto object = document.object();
	const auto format = object.value(u"format"_q);
	if (!format.isString()) {
		return Failure(ErrorCode::InvalidEnvelope, u"format"_q);
	}
	const auto legacy = (format.toString()
		== QString::fromLatin1(kLegacyEnvelopeFormat));
	if (format.toString() != QString::fromLatin1(kEnvelopeFormat) && !legacy) {
		return Failure(ErrorCode::UnsupportedEnvelope, format.toString());
	}
	const auto sessionString = object.value(u"sessionString"_q);
	const auto accountId = [&] {
		const auto direct = ReadJsonString(object.value(u"accountId"_q));
		return direct.isEmpty()
			? ReadJsonString(object.value(u"id"_q))
			: direct;
	}();
	if (accountId.isEmpty()) {
		return Failure(ErrorCode::InvalidEnvelope, u"accountId"_q);
	}
	const auto declaredUser = object.value(u"userId"_q);
	const auto userId = ReadJsonInteger(
		declaredUser.isUndefined()
			? QJsonValue(accountId)
			: declaredUser,
		ErrorCode::InvalidUserId,
		u"userId"_q);
	if (!sessionString.isString() || sessionString.toString().isEmpty()) {
		return Failure(ErrorCode::InvalidEnvelope, u"sessionString"_q);
	} else if (!userId || !*userId) {
		return userId
			? Failure(ErrorCode::InvalidUserId, u"userId"_q)
			: base::make_unexpected(userId.error());
	}
	const auto decoded = DecodeSessionString(sessionString.toString());
	if (!decoded) {
		return base::make_unexpected(decoded.error());
	} else if (decoded->userId != *userId) {
		return Failure(ErrorCode::EnvelopeUserMismatch);
	}

	const auto id = ReadJsonString(object.value(u"id"_q));
	const auto slotValue = object.value(u"slot"_q);
	const auto slot = slotValue.isUndefined()
		? base::expected<uint64, Error>(uint64(0))
		: ReadJsonInteger(
			slotValue,
			ErrorCode::InvalidEnvelope,
			u"slot"_q);
	const auto name = object.value(u"name"_q);
	const auto phone = object.value(u"phone"_q);
	const auto storage = object.value(u"storage"_q);
	const auto createdAt = object.value(u"createdAt"_q);
	if (!slot
		|| *slot > uint64(std::numeric_limits<int>::max())
		|| (!name.isUndefined() && !name.isString())
		|| !IsStringOrNull(phone)
		|| (!storage.isUndefined()
			&& (!storage.isString()
				|| (storage.toString() != u"local"_q
					&& storage.toString() != u"synced"_q)))) {
		return Failure(ErrorCode::InvalidEnvelope);
	}
	auto timestamp = createdAt.isString()
		? QDateTime::fromString(createdAt.toString(), Qt::ISODateWithMs)
		: QDateTime();
	if (!timestamp.isValid() && legacy) {
		timestamp = QDateTime::fromMSecsSinceEpoch(0, QTimeZone::UTC);
	} else if (!timestamp.isValid()) {
		return Failure(ErrorCode::InvalidEnvelope, u"createdAt"_q);
	}

	auto result = Envelope();
	result.id = id.isEmpty() ? accountId : id;
	result.accountId = accountId;
	result.slot = int(*slot);
	result.userId = *userId;
	result.name = name.isString() ? name.toString() : accountId;
	if (phone.isString()) {
		result.phone = phone.toString();
	}
	result.storage = storage.isString() ? storage.toString() : u"local"_q;
	result.createdAt = timestamp;
	result.sessionString = sessionString.toString();
	return result;
}

base::expected<QByteArray, Error> EncodeEnvelope(
		const Envelope &envelope) {
	const auto decoded = DecodeSessionString(envelope.sessionString);
	if (!decoded) {
		return base::make_unexpected(decoded.error());
	} else if (!envelope.userId
		|| envelope.userId > kMaximumExactJsonInteger
		|| decoded->userId != envelope.userId) {
		return Failure(ErrorCode::EnvelopeUserMismatch);
	} else if (envelope.slot < 0
		|| (envelope.storage != u"local"_q
			&& envelope.storage != u"synced"_q)
		|| !envelope.createdAt.isValid()) {
		return Failure(ErrorCode::InvalidEnvelope);
	}

	auto object = QJsonObject();
	object.insert(u"format"_q, QString::fromLatin1(kEnvelopeFormat));
	object.insert(u"id"_q, envelope.id);
	object.insert(u"accountId"_q, envelope.accountId);
	object.insert(u"slot"_q, envelope.slot);
	object.insert(u"userId"_q, double(envelope.userId));
	object.insert(u"name"_q, envelope.name);
	object.insert(
		u"phone"_q,
		envelope.phone
			? QJsonValue(*envelope.phone)
			: QJsonValue(QJsonValue::Null));
	object.insert(u"storage"_q, envelope.storage);
	object.insert(
		u"createdAt"_q,
		envelope.createdAt.toUTC().toString(Qt::ISODateWithMs));
	object.insert(u"sessionString"_q, envelope.sessionString);
	return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

} // namespace Ayu::SessionTransfer
