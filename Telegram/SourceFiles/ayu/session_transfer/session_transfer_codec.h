/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/expected.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QString>

#include <optional>

namespace Ayu::SessionTransfer {

inline constexpr auto kEnvelopeFormat =
	"mithka.tdlib.session_string.v2.explicit_consent";
inline constexpr auto kLegacyEnvelopeFormat =
	"mithka.tdlib.session_string.v1";

enum class ErrorCode {
	InvalidBase64,
	InvalidSize,
	InvalidDcId,
	InvalidApiId,
	InvalidTestMode,
	InvalidAuthKey,
	InvalidUserId,
	InvalidBotFlag,
	InvalidJson,
	InvalidEnvelope,
	UnsupportedEnvelope,
	EnvelopeUserMismatch,
};

struct Error {
	ErrorCode code = ErrorCode::InvalidEnvelope;
	QString detail;
};

struct SessionData {
	int dcId = 0;
	uint32 apiId = 0;
	bool testMode = false;
	QByteArray authKey;
	uint64 userId = 0;
	bool bot = false;
};

struct Envelope {
	QString id;
	QString accountId;
	int slot = 0;
	uint64 userId = 0;
	QString name;
	std::optional<QString> phone;
	QString storage;
	QDateTime createdAt;
	QString sessionString;
};

[[nodiscard]] base::expected<SessionData, Error> DecodeSessionString(
	QStringView value);
[[nodiscard]] base::expected<QString, Error> EncodeSessionString(
	const SessionData &data);

[[nodiscard]] base::expected<Envelope, Error> DecodeEnvelope(
	const QByteArray &json);
[[nodiscard]] base::expected<QByteArray, Error> EncodeEnvelope(
	const Envelope &envelope);

} // namespace Ayu::SessionTransfer
