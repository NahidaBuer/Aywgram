#include "ayu/cloud/ayu_cloud_storage.h"

#include "apiwrap.h"
#include "ayu/cloud/ayu_cloud_config.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace AyuCloud {
namespace {

Error Failure(ErrorCode code, QString details = {}) {
	return {
		.code = code,
		.details = std::move(details),
	};
}

QJsonArray KeysArray(const std::vector<QString> &keys) {
	auto result = QJsonArray();
	for (const auto &key : keys) {
		result.push_back(key);
	}
	return result;
}

} // namespace

Storage::Storage(not_null<Main::Session*> session)
: _session(session) {
}

bool Storage::available() const {
	return Config::Configured() && !_session->isTestMode();
}

bool Storage::validKey(const QString &key) const {
	static const auto expression = QRegularExpression(u"^[A-Za-z0-9_-]{1,128}$"_q);
	return expression.match(key).hasMatch();
}

void Storage::withBot(BotCallback callback) {
	if (!available()) {
		callback(nullptr, Failure(ErrorCode::BotUnavailable));
		return;
	}
	if (_resolvedBot) {
		callback(_resolvedBot, std::nullopt);
		return;
	}
	_resolveCallbacks.push_back(std::move(callback));
	if (_resolving) {
		return;
	}
	_resolving = true;
	const auto weak = base::make_weak(this);
	_session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(Config::kHelperBotUsername),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		if (!weak) {
			return;
		}
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			const auto peer = _session->data().peerLoaded(
				peerFromMTP(data.vpeer()));
			const auto bot = peer ? peer->asUser() : nullptr;
			if (!bot
				|| !bot->isBot()
				|| bot->id.value != peerFromUser(Config::kHelperBotId).value) {
				finishResolve(nullptr, Failure(ErrorCode::BotUnavailable));
			} else {
				finishResolve(bot, std::nullopt);
			}
		});
	}).fail([=](const MTP::Error &error) {
		if (weak) {
			finishResolve(nullptr, Failure(
				error.type() == u"USERNAME_NOT_OCCUPIED"_q
					? ErrorCode::BotUnavailable
					: ErrorCode::Network,
				error.type()));
		}
	}).send();
}

void Storage::finishResolve(UserData *bot, std::optional<Error> error) {
	_resolving = false;
	_resolvedBot = error ? nullptr : bot;
	auto callbacks = base::take(_resolveCallbacks);
	for (auto &callback : callbacks) {
		callback(bot, error);
	}
}

void Storage::invoke(
		UserData *bot,
		QString method,
		QByteArray params,
		Fn<void(base::expected<QByteArray, Error>)> done) {
	if (!bot) {
		done(base::unexpected(Failure(ErrorCode::BotUnavailable)));
		return;
	}
	const auto weak = base::make_weak(this);
	_session->api().request(MTPbots_InvokeWebViewCustomMethod(
		bot->inputUser(),
		MTP_string(method),
		MTP_dataJSON(MTP_bytes(params))
	)).done([=](const MTPDataJSON &result) {
		if (weak) {
			done(result.data().vdata().v);
		}
	}).fail([=](const MTP::Error &error) {
		if (weak) {
			done(base::unexpected(Failure(
				error.type() == u"METHOD_INVALID"_q
					? ErrorCode::BotUnavailable
					: ErrorCode::Network,
				error.type())));
		}
	}).send();
}

void Storage::setItem(
		QString key,
		QString value,
		Fn<void(EmptyResult)> done) {
	if (!validKey(key) || value.size() > 4096) {
		done(base::unexpected(Failure(ErrorCode::InvalidStorageResponse)));
		return;
	}
	withBot([=, key = std::move(key), value = std::move(value)](
			UserData *bot,
			std::optional<Error> error) mutable {
		if (error) {
			done(base::unexpected(*error));
			return;
		}
		const auto params = QJsonDocument(QJsonObject{
			{ u"key"_q, key },
			{ u"value"_q, value },
		}).toJson(QJsonDocument::Compact);
		invoke(bot, u"saveStorageValue"_q, params, [=](auto result) {
			done(result ? EmptyResult(true) : base::unexpected(result.error()));
		});
	});
}

void Storage::getItems(
		std::vector<QString> keys,
		Fn<void(ValuesResult)> done) {
	if (keys.empty() || keys.size() > kMaximumChunks + 1
		|| ranges::any_of(keys, [=](const QString &key) {
			return !validKey(key);
		})) {
		done(base::unexpected(Failure(ErrorCode::InvalidStorageResponse)));
		return;
	}
	withBot([=, keys = std::move(keys)](
			UserData *bot,
			std::optional<Error> error) mutable {
		if (error) {
			done(base::unexpected(*error));
			return;
		}
		const auto params = QJsonDocument(QJsonObject{
			{ u"keys"_q, KeysArray(keys) },
		}).toJson(QJsonDocument::Compact);
		invoke(bot, u"getStorageValues"_q, params, [=](auto result) {
			if (!result) {
				done(base::unexpected(result.error()));
				return;
			}
			auto parseError = QJsonParseError();
			const auto document = QJsonDocument::fromJson(*result, &parseError);
			if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
				done(base::unexpected(Failure(ErrorCode::InvalidStorageResponse)));
				return;
			}
			auto values = QMap<QString, QString>();
			const auto object = document.object();
			for (const auto &key : keys) {
				if (const auto value = object.value(key); value.isString()) {
					values.insert(key, value.toString());
				}
			}
			done(std::move(values));
		});
	});
}

void Storage::deleteItems(
		std::vector<QString> keys,
		Fn<void(EmptyResult)> done) {
	if (keys.empty()
		|| ranges::any_of(keys, [=](const QString &key) {
			return !validKey(key);
		})) {
		done(base::unexpected(Failure(ErrorCode::InvalidStorageResponse)));
		return;
	}
	withBot([=, keys = std::move(keys)](
			UserData *bot,
			std::optional<Error> error) mutable {
		if (error) {
			done(base::unexpected(*error));
			return;
		}
		const auto params = QJsonDocument(QJsonObject{
			{ u"keys"_q, KeysArray(keys) },
		}).toJson(QJsonDocument::Compact);
		invoke(bot, u"deleteStorageValues"_q, params, [=](auto result) {
			done(result ? EmptyResult(true) : base::unexpected(result.error()));
		});
	});
}

void Storage::getKeys(Fn<void(KeysResult)> done) {
	withBot([=](UserData *bot, std::optional<Error> error) {
		if (error) {
			done(base::unexpected(*error));
			return;
		}
		invoke(bot, u"getStorageKeys"_q, "{}", [=](auto result) {
			if (!result) {
				done(base::unexpected(result.error()));
				return;
			}
			auto parseError = QJsonParseError();
			const auto document = QJsonDocument::fromJson(*result, &parseError);
			if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
				done(base::unexpected(Failure(ErrorCode::InvalidStorageResponse)));
				return;
			}
			auto keys = std::vector<QString>();
			for (const auto &value : document.array()) {
				if (value.isString() && validKey(value.toString())) {
					keys.push_back(value.toString());
				}
			}
			done(std::move(keys));
		});
	});
}

} // namespace AyuCloud
