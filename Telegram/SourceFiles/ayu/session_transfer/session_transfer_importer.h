/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ayu/session_transfer/session_transfer_codec.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "mtproto/mtp_instance.h"

namespace Main {
class Account;
} // namespace Main

namespace MTP {
class Sender;
} // namespace MTP

namespace Ayu::SessionTransfer {

enum class ImportStage {
	Validating,
	CheckingDataCenter,
	MigratingAuthorization,
	VerifyingUser,
	Completing,
};

enum class ImportErrorCode {
	BotSession,
	Network,
	InvalidMigration,
	MigrationExportRejected,
	MigrationFailed,
	UserMismatch,
	Timeout,
	Cancelled,
};

struct ImportError {
	ImportErrorCode code = ImportErrorCode::Network;
	QString detail;
};

struct ImportedAuthorization {
	MTP::Instance::Fields fields;
	MTPUser user;
};

class Importer final : public base::has_weak_ptr {
public:
	using Progress = Fn<void(ImportStage)>;
	using Done = Fn<void(
		base::expected<ImportedAuthorization, ImportError>)>;

	Importer(SessionData data, Progress progress, Done done);
	~Importer();

	void start();
	void cancel();

private:
	void checkDataCenter();
	void transferAuthorization(MTP::DcId targetDcId);
	void importAuthorization(
		MTP::DcId targetDcId,
		const MTPauth_ExportedAuthorization &authorization);
	void verifyUser(MTP::DcId dcId);
	void fail(ImportErrorCode code, QString detail = {});
	void finish(ImportedAuthorization result);
	void setStage(ImportStage stage);

	SessionData _data;
	Progress _progress;
	Done _done;
	std::unique_ptr<MTP::Instance> _instance;
	std::unique_ptr<MTP::Sender> _sender;
	base::Timer _timeout;
	bool _started = false;
	bool _finished = false;

};

[[nodiscard]] base::expected<SessionData, Error> CurrentSessionData(
	not_null<Main::Account*> account);

} // namespace Ayu::SessionTransfer
