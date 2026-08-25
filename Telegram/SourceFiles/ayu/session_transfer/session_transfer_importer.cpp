/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ayu/session_transfer/session_transfer_importer.h"

#include "base/platform/base_platform_info.h"
#include "config.h"
#include "data/data_user.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/sender.h"

#include <algorithm>
#include <cstring>

namespace Ayu::SessionTransfer {
namespace {

constexpr auto kImportTimeout = crl::time(120 * 1000);

[[nodiscard]] MTP::Environment EnvironmentFor(const SessionData &data) {
	return data.testMode
		? MTP::Environment::Test
		: MTP::Environment::Production;
}

[[nodiscard]] std::shared_ptr<MTP::AuthKey> MakeAuthKey(
		const SessionData &data) {
	auto keyData = MTP::AuthKey::Data();
	std::memcpy(
		keyData.data(),
		data.authKey.constData(),
		MTP::AuthKey::kSize);
	return std::make_shared<MTP::AuthKey>(
		MTP::AuthKey::Type::Generated,
		data.dcId,
		keyData);
}

} // namespace

Importer::Importer(SessionData data, Progress progress, Done done)
: _data(std::move(data))
, _progress(std::move(progress))
, _done(std::move(done))
, _timeout([=] { fail(ImportErrorCode::Timeout); }) {
}

Importer::~Importer() {
	if (_started && !_finished) {
		_finished = true;
		_timeout.cancel();
		_done = nullptr;
		_sender = nullptr;
		_instance = nullptr;
	}
}

void Importer::start() {
	Expects(!_started);
	_started = true;
	setStage(ImportStage::Validating);
	if (_data.bot) {
		fail(ImportErrorCode::BotSession);
		return;
	}

	auto fields = MTP::Instance::Fields();
	fields.config = std::make_unique<MTP::Config>(EnvironmentFor(_data));
	fields.mainDcId = _data.dcId;
	fields.keys.push_back(MakeAuthKey(_data));
	fields.deviceModel = Platform::DeviceModelPretty();
	fields.systemVersion = Platform::SystemVersionPretty();
	_instance = std::make_unique<MTP::Instance>(
		MTP::Instance::Mode::Normal,
		std::move(fields));
	_sender = std::make_unique<MTP::Sender>(_instance.get());
	_timeout.callOnce(kImportTimeout);
	checkDataCenter();
}

void Importer::cancel() {
	if (_finished) {
		return;
	}
	fail(ImportErrorCode::Cancelled);
}

void Importer::checkDataCenter() {
	setStage(ImportStage::CheckingDataCenter);
	const auto weak = base::make_weak(this);
	_sender->request(MTPupdates_GetState(
	)).done([=] {
		if (const auto strong = weak.get()) {
			strong->verifyUser(strong->_data.dcId);
		}
	}).fail([=](const MTP::Error &error) {
		if (const auto strong = weak.get()) {
			if (const auto targetDcId = MTP::MigrateDcId(error)) {
				strong->transferAuthorization(*targetDcId);
			} else {
				strong->fail(ImportErrorCode::Network, error.type());
			}
		}
	}).handleAllErrors().handleMigrateErrors().toDC(_data.dcId).send();
}

void Importer::transferAuthorization(MTP::DcId targetDcId) {
	if (targetDcId <= 0 || targetDcId == _data.dcId) {
		fail(ImportErrorCode::InvalidMigration);
		return;
	}
	setStage(ImportStage::MigratingAuthorization);
	const auto weak = base::make_weak(this);
	_sender->request(MTPauth_ExportAuthorization(
		MTP_int(targetDcId)
	)).done([=](const MTPauth_ExportedAuthorization &result) {
		if (const auto strong = weak.get()) {
			strong->importAuthorization(targetDcId, result);
		}
	}).fail([=](const MTP::Error &error) {
		if (const auto strong = weak.get()) {
			strong->fail(
				MTP::MigrateDcId(error)
					? ImportErrorCode::MigrationExportRejected
					: ImportErrorCode::MigrationFailed,
				error.type());
		}
	}).handleAllErrors().handleMigrateErrors().toDC(_data.dcId).send();
}

void Importer::importAuthorization(
		MTP::DcId targetDcId,
		const MTPauth_ExportedAuthorization &authorization) {
	const auto &data = authorization.c_auth_exportedAuthorization();
	const auto weak = base::make_weak(this);
	_sender->request(MTPauth_ImportAuthorization(
		data.vid(),
		data.vbytes()
	)).done([=] {
		if (const auto strong = weak.get()) {
			strong->_instance->setMainDcId(targetDcId);
			strong->verifyUser(targetDcId);
		}
	}).fail([=](const MTP::Error &error) {
		if (const auto strong = weak.get()) {
			strong->fail(ImportErrorCode::MigrationFailed, error.type());
		}
	}).handleAllErrors().handleMigrateErrors().toDC(targetDcId).send();
}

void Importer::verifyUser(MTP::DcId dcId) {
	setStage(ImportStage::VerifyingUser);
	const auto weak = base::make_weak(this);
	_sender->request(MTPusers_GetUsers(
		MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())
	)).done([=](const MTPVector<MTPUser> &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		auto verified = std::optional<MTPUser>();
		for (const auto &user : result.v) {
			user.match([&](const MTPDuser &data) {
				if (data.is_self()
					&& uint64(data.vid().v) == strong->_data.userId) {
					verified = user;
				}
			}, [](const MTPDuserEmpty &) {
			});
		}
		if (!verified) {
			strong->fail(ImportErrorCode::UserMismatch);
			return;
		}
		strong->setStage(ImportStage::Completing);
		auto keys = strong->_instance->getKeysForWrite();
		if (!std::any_of(
			keys.cbegin(),
			keys.cend(),
			[=](const auto &key) {
				return MTP::BareDcId(key->dcId()) == dcId;
			})) {
			strong->fail(ImportErrorCode::MigrationFailed);
			return;
		}
		auto fields = MTP::Instance::Fields();
		fields.config = std::make_unique<MTP::Config>(
			strong->_instance->config());
		fields.mainDcId = dcId;
		fields.keys = std::move(keys);
		fields.deviceModel = Platform::DeviceModelPretty();
		fields.systemVersion = Platform::SystemVersionPretty();
		strong->finish(ImportedAuthorization{
			.fields = std::move(fields),
			.user = std::move(*verified),
		});
	}).fail([=](const MTP::Error &error) {
		if (const auto strong = weak.get()) {
			strong->fail(ImportErrorCode::Network, error.type());
		}
	}).handleAllErrors().handleMigrateErrors().toDC(dcId).send();
}

void Importer::fail(ImportErrorCode code, QString detail) {
	if (_finished) {
		return;
	}
	_finished = true;
	_timeout.cancel();
	_sender = nullptr;
	_instance = nullptr;
	auto done = base::take(_done);
	if (done) {
		done(base::make_unexpected(ImportError{
			.code = code,
			.detail = std::move(detail),
		}));
	}
}

void Importer::finish(ImportedAuthorization result) {
	if (_finished) {
		return;
	}
	_finished = true;
	_timeout.cancel();
	_sender = nullptr;
	_instance = nullptr;
	auto done = base::take(_done);
	if (done) {
		done(std::move(result));
	}
}

void Importer::setStage(ImportStage stage) {
	if (_progress) {
		_progress(stage);
	}
}

base::expected<SessionData, Error> CurrentSessionData(
		not_null<Main::Account*> account) {
	if (!account->sessionExists()) {
		return base::make_unexpected(Error{ ErrorCode::InvalidUserId });
	}
	const auto dcId = MTP::BareDcId(account->mtp().mainDcId());
	const auto keys = account->mtp().getKeysForWrite();
	const auto key = std::find_if(
		keys.cbegin(),
		keys.cend(),
		[=](const auto &candidate) {
			return MTP::BareDcId(candidate->dcId()) == dcId;
		});
	if (key == keys.cend()) {
		return base::make_unexpected(Error{ ErrorCode::InvalidAuthKey });
	}
	const auto bytes = (*key)->data();
	if (bytes.size() != MTP::AuthKey::kSize) {
		return base::make_unexpected(Error{ ErrorCode::InvalidAuthKey });
	}

	auto result = SessionData();
	result.dcId = dcId;
	result.apiId = ApiId;
	result.testMode = account->mtp().isTestMode();
	result.authKey = QByteArray(
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size());
	result.userId = account->session().userId().bare;
	result.bot = account->session().user()->isBot();
	return result;
}

} // namespace Ayu::SessionTransfer
