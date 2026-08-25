// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/session_transfer_box.h"

#include "ayu/session_transfer/session_transfer_codec.h"
#include "ayu/session_transfer/session_transfer_importer.h"
#include "base/qt/qt_common_adapters.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "lang_auto.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"

#include "styles/style_boxes.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

#include <QtCore/QFile>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Ayu::SessionTransfer {
namespace {

struct ImportState {
	std::shared_ptr<Ui::Show> show;
	Main::Account *introAccount = nullptr;
	Ui::InputField *field = nullptr;
	Ui::RoundButton *importButton = nullptr;
	Ui::RoundButton *fileButton = nullptr;
	rpl::variable<QString> status;
	std::shared_ptr<Importer> importer;
	bool busy = false;
};

[[nodiscard]] QString CodecErrorText(const Error &error) {
	switch (error.code) {
	case ErrorCode::InvalidJson:
		return tr::ayu_SessionTransferInvalidJson(tr::now);
	case ErrorCode::UnsupportedEnvelope:
		return tr::ayu_SessionTransferUnsupportedFormat(tr::now);
	case ErrorCode::EnvelopeUserMismatch:
		return tr::ayu_SessionTransferUserMismatch(tr::now);
	default:
		return tr::ayu_SessionTransferInvalidSession(tr::now);
	}
}

[[nodiscard]] QString ImportErrorText(const ImportError &error) {
	switch (error.code) {
	case ImportErrorCode::BotSession:
		return tr::ayu_SessionTransferBotUnsupported(tr::now);
	case ImportErrorCode::InvalidMigration:
	case ImportErrorCode::MigrationExportRejected:
	case ImportErrorCode::MigrationFailed:
		return tr::ayu_SessionTransferMigrationFailed(tr::now);
	case ImportErrorCode::UserMismatch:
		return tr::ayu_SessionTransferUserMismatch(tr::now);
	case ImportErrorCode::Timeout:
		return tr::ayu_SessionTransferTimeout(tr::now);
	case ImportErrorCode::Cancelled:
		return QString();
	case ImportErrorCode::Network:
		return error.detail.isEmpty()
			? tr::ayu_SessionTransferNetworkError(tr::now)
			: tr::ayu_SessionTransferNetworkErrorDetails(
				tr::now,
				lt_error,
				error.detail);
	}
	Unexpected("ImportErrorCode value");
}

[[nodiscard]] QString StageText(ImportStage stage) {
	switch (stage) {
	case ImportStage::Validating:
		return tr::ayu_SessionTransferStatusValidating(tr::now);
	case ImportStage::CheckingDataCenter:
		return tr::ayu_SessionTransferStatusCheckingDc(tr::now);
	case ImportStage::MigratingAuthorization:
		return tr::ayu_SessionTransferStatusMigrating(tr::now);
	case ImportStage::VerifyingUser:
		return tr::ayu_SessionTransferStatusVerifying(tr::now);
	case ImportStage::Completing:
		return tr::ayu_SessionTransferStatusCompleting(tr::now);
	}
	Unexpected("ImportStage value");
}

[[nodiscard]] MTP::Environment EnvironmentFor(const SessionData &data) {
	return data.testMode
		? MTP::Environment::Test
		: MTP::Environment::Production;
}

[[nodiscard]] Main::Account *FindDuplicate(
		Main::Domain &domain,
		const SessionData &data) {
	const auto environment = EnvironmentFor(data);
	for (const auto &[index, account] : domain.accounts()) {
		if (const auto session = account->maybeSession()) {
			if (account->mtp().environment() == environment
				&& session->userId().bare == data.userId) {
				return account.get();
			}
		}
	}
	return nullptr;
}

[[nodiscard]] Main::Account *FindReusable(
		Main::Domain &domain,
		MTP::Environment environment) {
	for (const auto &[index, account] : domain.accounts()) {
		if (!account->sessionExists()
			&& account->mtp().environment() == environment) {
			return account.get();
		}
	}
	return nullptr;
}

[[nodiscard]] base::expected<SessionData, Error> ParseInput(
		const QString &text) {
	const auto trimmed = text.trimmed();
	if (trimmed.startsWith('{')) {
		const auto envelope = DecodeEnvelope(trimmed.toUtf8());
		return envelope
			? DecodeSessionString(envelope->sessionString)
			: base::make_unexpected(envelope.error());
	}
	return DecodeSessionString(trimmed);
}

void SetBusy(not_null<ImportState*> state, bool busy) {
	state->busy = busy;
	state->field->setDisabled(busy);
	state->importButton->setDisabled(busy);
	state->fileButton->setDisabled(busy);
}

void ApplyImported(
		not_null<Ui::GenericBox*> box,
		not_null<ImportState*> state,
		SessionData data,
		ImportedAuthorization result) {
	auto &domain = Core::App().domain();
	if (const auto duplicate = FindDuplicate(domain, data)) {
		state->show->showToast(tr::ayu_SessionTransferDuplicate(tr::now));
		domain.activate(duplicate);
		box->closeBox();
		return;
	}

	const auto environment = EnvironmentFor(data);
	auto account = state->introAccount;
	if (!account) {
		account = FindReusable(domain, environment);
		if (!account) {
			if (domain.accountsAuthedCount() >= domain.maxAccounts()) {
				state->status = tr::ayu_SessionTransferAccountLimit(tr::now);
				SetBusy(state, false);
				return;
			}
			account = domain.add(environment);
		}
	}

	const auto show = state->show;
	const auto weakBox = base::make_weak(box);
	domain.activate(account);
	account->applyImportedAuthorization(
		std::move(result.fields),
		result.user);
	show->showToast(tr::ayu_SessionTransferImportDone(tr::now));
	if (weakBox) {
		weakBox->closeBox();
	}
}

void StartImport(
		not_null<Ui::GenericBox*> box,
		not_null<ImportState*> state,
		SessionData data) {
	if (state->busy) {
		return;
	}
	auto &domain = Core::App().domain();
	if (const auto duplicate = FindDuplicate(domain, data)) {
		state->show->showToast(tr::ayu_SessionTransferDuplicate(tr::now));
		domain.activate(duplicate);
		box->closeBox();
		return;
	}
	if (!state->introAccount
		&& !FindReusable(domain, EnvironmentFor(data))
		&& domain.accountsAuthedCount() >= domain.maxAccounts()) {
		state->status = tr::ayu_SessionTransferAccountLimit(tr::now);
		return;
	}

	SetBusy(state, true);
	const auto weak = base::make_weak(box);
	state->importer = std::make_shared<Importer>(
		data,
		[=](ImportStage stage) {
			if (weak) {
				state->status = StageText(stage);
			}
		},
		[=](base::expected<ImportedAuthorization, ImportError> result) mutable {
			if (!weak) {
				return;
			}
			state->importer = nullptr;
			if (!result) {
				const auto text = ImportErrorText(result.error());
				if (!text.isEmpty()) {
					state->status = text;
				}
				SetBusy(state, false);
				return;
			}
			ApplyImported(box, state, data, std::move(*result));
		});
	state->importer->start();
}

void FillImportBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		Main::Account *introAccount,
		Fn<void()> closed) {
	const auto state = box->lifetime().make_state<ImportState>();
	state->show = std::move(show);
	state->introAccount = introAccount;
	state->status = tr::ayu_SessionTransferPasteHint(tr::now);
	if (closed) {
		box->boxClosing(
		) | rpl::on_next(std::move(closed), box->lifetime());
	}

	box->setTitle(tr::ayu_SessionTransferImportTitle());
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::ayu_SessionTransferRisk(tr::rich),
		st::boxLabel));
	state->field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		tr::ayu_SessionTransferInputPlaceholder()));
	state->field->setMinHeight(st::introSessionTransferInputHeight);
	state->field->setMaxHeight(st::introSessionTransferInputHeight);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		state->status.value(),
		st::boxLabel));

	state->importButton = box->addButton(
		tr::ayu_SessionTransferImport(),
		[=] {
			const auto parsed = ParseInput(state->field->getLastText());
			if (!parsed) {
				state->status = CodecErrorText(parsed.error());
				return;
			}
			state->show->show(Ui::MakeConfirmBox({
				.text = tr::ayu_SessionTransferRiskConfirm(tr::rich),
				.confirmed = [=, data = *parsed](Fn<void()> &&close) mutable {
					close();
					StartImport(box, state, std::move(data));
				},
				.confirmText = tr::ayu_SessionTransferImport(),
			}));
		});
	state->fileButton = box->addButton(
		tr::ayu_SessionTransferChooseFile(),
		[=] {
			FileDialog::GetOpenPath(
				Core::App().getFileDialogParent(),
				tr::ayu_SessionTransferChooseFile(tr::now),
				u"JSON (*.json);;All files (*.*)"_q,
				[=](FileDialog::OpenResult &&result) {
					auto content = std::move(result.remoteContent);
					if (content.isEmpty() && result.paths.isEmpty()) {
						return;
					}
					if (content.isEmpty() && !result.paths.isEmpty()) {
						auto file = QFile(result.paths.front());
						if (file.open(QIODevice::ReadOnly)) {
							content = file.readAll();
						}
					}
					const auto envelope = DecodeEnvelope(content);
					if (!envelope) {
						state->status = CodecErrorText(envelope.error());
						return;
					}
					state->field->setText(envelope->sessionString);
					state->status = tr::ayu_SessionTransferFileReady(tr::now);
				});
		});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setShowFinishedCallback([=] { state->field->setFocusFast(); });
}

[[nodiscard]] base::expected<QString, Error> EncodedCurrentSession(
		not_null<Main::Account*> account) {
	const auto data = CurrentSessionData(account);
	return data
		? EncodeSessionString(*data)
		: base::make_unexpected(data.error());
}

} // namespace

void ShowImportBox(
		std::shared_ptr<Ui::Show> show,
		Main::Account *introAccount,
		Fn<void()> closed) {
	show->show(Box(
		FillImportBox,
		show,
		introAccount,
		std::move(closed)));
}

void ConfirmCopySession(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Account*> account) {
	show->show(Ui::MakeConfirmBox({
		.text = tr::ayu_SessionTransferRiskConfirm(tr::rich),
		.confirmed = [=](Fn<void()> &&close) {
			const auto encoded = EncodedCurrentSession(account);
			if (!encoded) {
				show->showToast(CodecErrorText(encoded.error()));
			} else {
				QGuiApplication::clipboard()->setText(*encoded);
				show->showToast(tr::lng_text_copied(tr::now));
			}
			close();
		},
		.confirmText = tr::lng_group_invite_context_copy(),
	}));
}

void ConfirmExportEnvelope(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Account*> account) {
	show->show(Ui::MakeConfirmBox({
		.text = tr::ayu_SessionTransferRiskConfirm(tr::rich),
		.confirmed = [=](Fn<void()> &&close) {
			close();
			const auto sessionString = EncodedCurrentSession(account);
			if (!sessionString) {
				show->showToast(CodecErrorText(sessionString.error()));
				return;
			}
			const auto user = account->session().user();
			auto envelope = Envelope();
			envelope.id = QString::number(user->id.value);
			envelope.accountId = envelope.id;
			envelope.slot = 0;
			envelope.userId = user->id.value;
			envelope.name = user->name();
			if (!user->phone().isEmpty()) {
				envelope.phone = user->phone();
			}
			envelope.storage = u"local"_q;
			envelope.createdAt = QDateTime::currentDateTimeUtc();
			envelope.sessionString = *sessionString;
			const auto json = EncodeEnvelope(envelope);
			if (!json) {
				show->showToast(CodecErrorText(json.error()));
				return;
			}
			FileDialog::GetWritePath(
				Core::App().getFileDialogParent(),
				tr::ayu_SessionTransferExportJson(tr::now),
				u"JSON (*.json)"_q,
				u"aywgram-session-%1.json"_q.arg(envelope.userId),
				[=, json = *json](QString &&path) {
					if (path.isEmpty()) {
						return;
					}
					auto file = QFile(path);
					if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
						|| file.write(json) != json.size()) {
						show->showToast(
							tr::ayu_SessionTransferFileWriteFailed(tr::now));
						return;
					}
					file.close();
					show->showToast(tr::lng_box_done(tr::now));
				});
		},
		.confirmText = tr::ayu_SessionTransferExportJson(),
	}));
}

} // namespace Ayu::SessionTransfer
