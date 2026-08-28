#include "ayu/ui/settings/settings_cloud_sync.h"

#include "ayu/cloud/ayu_settings_sync.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_main.h"
#include "core/application.h"
#include "data/data_user.h"
#include "lang_auto.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QLocale>

namespace Settings {
namespace {

using namespace Builder;

QString DetailText(const QString &details) {
	if (details == u"service_unconfigured"_q) {
		return tr::ayu_CloudSyncNotConfigured(tr::now);
	} else if (details == u"sync_account_unavailable"_q) {
		return tr::ayu_CloudSyncErrorAccountUnavailable(tr::now);
	} else if (details == u"network_error"_q) {
		return tr::ayu_CloudSyncErrorNetwork(tr::now);
	} else if (details == u"backup_not_found"_q) {
		return tr::ayu_CloudSyncErrorNotFound(tr::now);
	} else if (details == u"backup_account_mismatch"_q) {
		return tr::ayu_CloudSyncErrorAccountMismatch(tr::now);
	} else if (details == u"revision_conflict"_q) {
		return tr::ayu_CloudSyncErrorConflict(tr::now);
	} else if (details == u"appearance_preserved"_q) {
		return tr::ayu_CloudSyncAppearancePreserved(tr::now);
	} else if (details == u"rollback_failed"_q
		|| details == u"restore_apply_failed"_q) {
		return tr::ayu_CloudSyncErrorRestore(tr::now);
	} else if (details == u"invalid_backup"_q
		|| details == u"missing_chunk"_q
		|| details == u"hash_mismatch"_q
		|| details == u"unsupported_schema"_q) {
		return tr::ayu_CloudSyncErrorInvalid(tr::now);
	}
	return details;
}

QString StatusText(const AyuCloud::SyncStatus &status) {
	using State = AyuCloud::SyncState;
	auto result = [&] {
		switch (status.state) {
		case State::Disabled: return tr::ayu_CloudSyncStatusDisabled(tr::now);
		case State::Checking: return tr::ayu_CloudSyncStatusChecking(tr::now);
		case State::Clean: return tr::ayu_CloudSyncStatusClean(tr::now);
		case State::Dirty: return tr::ayu_CloudSyncStatusDirty(tr::now);
		case State::Uploading: return tr::ayu_CloudSyncStatusUploading(tr::now);
		case State::RemoteNewer: return tr::ayu_CloudSyncStatusRemoteNewer(tr::now);
		case State::Conflict: return tr::ayu_CloudSyncStatusConflict(tr::now);
		case State::RestorePending: return tr::ayu_CloudSyncStatusRestart(tr::now);
		case State::Error: return tr::ayu_CloudSyncStatusError(tr::now);
		}
		Unexpected("Unknown settings sync state.");
	}();
	if (!status.details.isEmpty()) {
		result += u" — "_q + DetailText(status.details);
	}
	return result;
}

QString DifferenceText(
		const AyuCloud::SyncStatus &status,
		AyuCloud::Category category) {
	if (!status.hasDifferences) {
		return {};
	}
	const auto index = [&] {
		switch (category) {
		case AyuCloud::Category::AyuGlobal: return 0;
		case AyuCloud::Category::TelegramGlobal: return 1;
		case AyuCloud::Category::AccountAndChats: return 2;
		case AyuCloud::Category::Appearance: return 3;
		}
		Unexpected("Unknown cloud settings category.");
	}();
	switch (status.differences[index]) {
	case AyuCloud::Difference::Same:
		return tr::ayu_CloudSyncDifferenceSame(tr::now);
	case AyuCloud::Difference::Different:
		return tr::ayu_CloudSyncDifferenceChanged(tr::now);
	case AyuCloud::Difference::LocalOnly:
		return tr::ayu_CloudSyncDifferenceLocalOnly(tr::now);
	case AyuCloud::Difference::RemoteOnly:
		return tr::ayu_CloudSyncDifferenceCloudOnly(tr::now);
	}
	Unexpected("Unknown cloud settings difference.");
}

void BuildMaster(SectionBuilder &builder) {
	const auto sync = &AyuCloud::SettingsSync::Instance();
	const auto controller = builder.controller();

	const auto master = builder.addButton({
		.id = u"ayu/cloudSync/enabled"_q,
		.altIds = { u"ayu/settingsSync"_q },
		.title = tr::ayu_CloudSyncEnabled(),
		.icon = { &st::menuIconSavedMessages },
		.toggled = sync->enabledValue(),
		.keywords = { u"backup"_q, u"cloud"_q, u"sync"_q },
	});
	if (master) {
		master->setDisabled(!sync->configured());
		master->toggledChanges(
		) | rpl::on_next([=](bool enabled) {
			if (enabled == sync->enabled()) {
				return;
			}
			if (!enabled) {
				sync->setEnabled(false);
				return;
			}
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_CloudSyncPrivacyWarning(),
				.confirmed = [=](Fn<void()> close) {
					sync->setEnabled(true);
					close();
				},
				.cancelled = [=](Fn<void()> close) {
					sync->setEnabled(false);
					close();
				},
				.confirmText = tr::ayu_CloudSyncEnableAction(),
			}));
		}, master->lifetime());
	}
	builder.addDividerText(sync->configured()
		? tr::ayu_CloudSyncDescription()
		: tr::ayu_CloudSyncNotConfigured());

	const auto automatic = builder.addButton({
		.id = u"ayu/cloudSync/automatic"_q,
		.title = tr::ayu_CloudSyncAutomatic(),
		.toggled = sync->automaticUploadValue(),
	});
	if (automatic) {
		automatic->toggledChanges(
		) | rpl::on_next([=](bool value) {
			if (value == sync->automaticUpload()) {
				return;
			}
			sync->setAutomaticUpload(value);
		}, automatic->lifetime());
	}
}

void BuildAccount(SectionBuilder &builder, AyuBuilder::AyuSectionBuilder &ayu) {
	const auto sync = &AyuCloud::SettingsSync::Instance();
	const auto domain = &builder.session()->domain();
	auto ids = std::vector<uint64_t>();
	auto names = std::vector<QString>();
	for (const auto account : domain->orderedAccounts()) {
		if (const auto session = account->maybeSession()) {
			ids.push_back(session->userId().bare);
			names.push_back(session->user()->name());
		}
	}
	if (ids.empty()) {
		return;
	}
	const auto selected = ranges::find(ids, sync->accountId());
	const auto initial = (selected == end(ids))
		? 0
		: int(selected - begin(ids));
	ayu.addChooseButton({
		.id = u"ayu/cloudSync/account"_q,
		.title = tr::ayu_CloudSyncAccount(),
		.boxTitle = tr::ayu_CloudSyncAccount(),
		.initialSelection = initial,
		.options = names,
		.setter = [ids = std::move(ids)](int index) {
			AyuCloud::SettingsSync::Instance().setAccountId(ids[index]);
		},
		.icon = { &st::menuIconProfile },
	});
}

void AddCategory(
		SectionBuilder &builder,
		QString id,
		rpl::producer<QString> title,
		AyuCloud::Category category) {
	const auto sync = &AyuCloud::SettingsSync::Instance();
	const auto controller = builder.controller();
	const auto bit = uint32_t(category);
	const auto button = builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.label = sync->statusValue() | rpl::map([=](const auto &status) {
			return DifferenceText(status, category);
		}),
		.toggled = sync->categoriesValue() | rpl::map([=](uint32_t mask) {
			return (mask & bit) != 0;
		}),
	});
	if (!button) {
		return;
	}
	button->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		const auto old = sync->categories();
		if (enabled == ((old & bit) != 0)) {
			return;
		}
		const auto proposed = enabled ? (old | bit) : (old & ~bit);
		if (!proposed) {
			sync->setCategories(old);
			return;
		}
		controller->show(Ui::MakeConfirmBox({
			.text = tr::ayu_CloudSyncCategoryChangeWarning(),
			.confirmed = [=](Fn<void()> close) {
				sync->setCategories(proposed);
				close();
			},
			.cancelled = [=](Fn<void()> close) {
				sync->setCategories(old);
				close();
			},
			.confirmText = tr::lng_settings_save(),
		}));
	}, button->lifetime());
}

void BuildCategories(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_CloudSyncCategories());
	AddCategory(
		builder,
		u"ayu/cloudSync/category/ayu"_q,
		tr::ayu_CloudSyncCategoryAyu(),
		AyuCloud::Category::AyuGlobal);
	AddCategory(
		builder,
		u"ayu/cloudSync/category/telegram"_q,
		tr::ayu_CloudSyncCategoryTelegram(),
		AyuCloud::Category::TelegramGlobal);
	AddCategory(
		builder,
		u"ayu/cloudSync/category/account"_q,
		tr::ayu_CloudSyncCategoryAccount(),
		AyuCloud::Category::AccountAndChats);
	AddCategory(
		builder,
		u"ayu/cloudSync/category/appearance"_q,
		tr::ayu_CloudSyncCategoryAppearance(),
		AyuCloud::Category::Appearance);

	const auto sync = &AyuCloud::SettingsSync::Instance();
	const auto proxies = builder.addButton({
		.id = u"ayu/cloudSync/proxies"_q,
		.title = tr::ayu_CloudSyncProxies(),
		.toggled = sync->syncProxiesValue(),
		.keywords = { u"proxy"_q, u"mtproto"_q, u"socks"_q },
	});
	if (proxies) {
		proxies->toggledChanges(
		) | rpl::on_next([=](bool value) {
			if (value != sync->syncProxies()) {
				sync->setSyncProxies(value);
			}
		}, proxies->lifetime());
	}
	builder.addDividerText(tr::ayu_CloudSyncProxiesDescription());
}

void BuildStatus(SectionBuilder &builder) {
	const auto sync = &AyuCloud::SettingsSync::Instance();
	const auto controller = builder.controller();
	if (controller) {
		sync->manualErrors(
		) | rpl::on_next([=](const QString &error) {
			controller->showToast(DetailText(error));
		}, builder.container()->lifetime());
		const auto prompted = builder.container()->lifetime().make_state<bool>(false);
		const auto restartPrompted = builder.container()->lifetime().make_state<bool>(false);
		sync->statusValue(
		) | rpl::on_next([=](const AyuCloud::SyncStatus &status) {
			if (!*restartPrompted
				&& status.state == AyuCloud::SyncState::RestorePending) {
				*restartPrompted = true;
				controller->show(Ui::MakeConfirmBox({
					.text = tr::ayu_CloudSyncRestartPrompt(),
					.confirmed = [](Fn<void()> close) {
						close();
						Core::Restart();
					},
					.confirmText = tr::lng_settings_restart_now(),
				}));
			}
			if (*prompted
				|| status.state != AyuCloud::SyncState::RemoteNewer
				|| status.localRevision
				|| !status.hasDifferences) {
				return;
			}
			*prompted = true;
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				const auto decided = box->lifetime().make_state<bool>(false);
				box->setTitle(tr::ayu_CloudSyncExistingTitle());
				box->addRow(object_ptr<Ui::FlatLabel>(
					box,
					tr::ayu_CloudSyncExistingDescription(),
					st::boxLabel));
				box->boxClosing(
				) | rpl::on_next([=] {
					if (!*decided) {
						sync->setEnabled(false);
					}
				}, box->lifetime());
				box->addButton(tr::ayu_CloudSyncUseCloud(), [=] {
					*decided = true;
					box->closeBox();
					sync->restoreNow();
				});
				box->addButton(tr::ayu_CloudSyncKeepLocal(), [=] {
					*decided = true;
					box->closeBox();
					controller->show(Ui::MakeConfirmBox({
						.text = tr::ayu_CloudSyncOverwriteWarning(),
						.confirmed = [=](Fn<void()> close) {
							close();
							sync->uploadNow(true);
						},
						.confirmText = tr::ayu_CloudSyncKeepLocal(),
						.confirmStyle = &st::attentionBoxButton,
					}));
				});
				box->addButton(tr::lng_cancel(), [=] {
					*decided = true;
					box->closeBox();
					sync->setEnabled(false);
				});
			}));
		}, builder.container()->lifetime());
	}
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_CloudSyncStatus());
	builder.addButton({
		.id = u"ayu/cloudSync/status"_q,
		.title = tr::ayu_CloudSyncCurrentStatus(),
		.icon = { &st::menuIconInfo },
		.label = sync->statusValue() | rpl::map(StatusText),
	});
	builder.addButton({
		.id = u"ayu/cloudSync/localRevision"_q,
		.title = tr::ayu_CloudSyncLocalRevision(),
		.label = sync->statusValue() | rpl::map([](const auto &status) {
			return status.localRevision
				? QString::number(status.localRevision)
				: QString::fromLatin1("—");
		}),
	});
	builder.addButton({
		.id = u"ayu/cloudSync/revision"_q,
		.title = tr::ayu_CloudSyncRemoteRevision(),
		.label = sync->statusValue() | rpl::map([](const AyuCloud::SyncStatus &status) {
			if (!status.remoteRevision) {
				return QString::fromLatin1("—");
			}
			auto result = QString::number(status.remoteRevision);
			if (status.remoteUpdatedAt) {
				result += u" · "_q + QLocale().toString(
					QDateTime::fromSecsSinceEpoch(status.remoteUpdatedAt),
					QLocale::ShortFormat);
			}
			return result;
		}),
	});
	builder.addButton({
		.id = u"ayu/cloudSync/device"_q,
		.title = tr::ayu_CloudSyncRemoteDevice(),
		.label = sync->statusValue() | rpl::map([](const auto &status) {
			return status.remoteDeviceId.isEmpty()
				? QString::fromLatin1("—")
				: status.remoteDeviceId;
		}),
	});
	builder.addButton({
		.id = u"ayu/cloudSync/upload"_q,
		.title = tr::ayu_CloudSyncUploadNow(),
		.icon = { &st::menuIconExport },
		.onClick = [=] {
			const auto state = sync->status().state;
			if (state != AyuCloud::SyncState::Conflict
				&& state != AyuCloud::SyncState::RemoteNewer) {
				sync->uploadNow();
				return;
			}
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_CloudSyncOverwriteWarning(),
				.confirmed = [=](Fn<void()> close) {
					close();
					sync->uploadNow(true);
				},
				.confirmText = tr::ayu_CloudSyncKeepLocal(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		},
	});
	builder.addButton({
		.id = u"ayu/cloudSync/restore"_q,
		.title = tr::ayu_CloudSyncCheckRestore(),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_CloudSyncRestoreWarning(),
				.confirmed = [=](Fn<void()> close) {
					close();
					sync->restoreNow();
				},
				.confirmText = tr::ayu_CloudSyncUseCloud(),
			}));
		},
	});
	builder.addButton({
		.id = u"ayu/cloudSync/restart"_q,
		.title = tr::lng_settings_restart_now(),
		.icon = { &st::menuIconRestartBot },
		.onClick = [] { Core::Restart(); },
		.shown = sync->statusValue() | rpl::map([](const auto &status) {
			return status.state == AyuCloud::SyncState::RestorePending;
		}),
	});
	builder.addButton({
		.id = u"ayu/cloudSync/rollback"_q,
		.title = tr::ayu_CloudSyncRollback(),
		.icon = { &st::menuIconRestore },
		.onClick = [=] { sync->rollbackPendingRestore(); },
		.shown = sync->statusValue() | rpl::map([](const auto &status) {
			return status.state == AyuCloud::SyncState::RestorePending;
		}),
	});
	builder.addButton({
		.id = u"ayu/cloudSync/delete"_q,
		.title = tr::ayu_CloudSyncDelete(),
		.icon = { &st::menuIconDelete },
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_CloudSyncDeleteWarning(),
				.confirmed = [=](Fn<void()> close) {
					close();
					sync->deleteRemote();
				},
				.confirmText = tr::ayu_CloudSyncDelete(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		},
	});
	if (controller && sync->enabled()) {
		crl::on_main([=] { sync->checkNow(false); });
	}
}

const auto kMeta = BuildHelper({
	.id = AyuCloudSync::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CloudSyncTitle,
	.icon = &st::menuIconSavedMessages,
}, [](SectionBuilder &builder) {
	auto ayu = AyuBuilder::AyuSectionBuilder(builder);
	builder.addSkip();
	BuildMaster(builder);
	BuildAccount(builder, ayu);
	BuildCategories(builder);
	BuildStatus(builder);
	builder.addSkip();
	builder.addDividerText(tr::ayu_CloudSyncExcluded());
});

} // namespace

AyuCloudSync::AyuCloudSync(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> AyuCloudSync::title() {
	return tr::ayu_CloudSyncTitle();
}

void AyuCloudSync::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
