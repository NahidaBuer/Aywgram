// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_general.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/features/link_rules/ayu_link_rules.h"
#include "ayu/features/link_rules/ayu_remote_metadata.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "apiwrap.h"
#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_text_entity.h"
#include "main/main_session.h"
#include "platform/platform_translate_provider.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <memory>
#include <optional>

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

nlohmann::json EditableRules() {
	auto result = AyuSettings::getInstance().linkRules();
	if (result.empty()) {
		result = {
			{ "pagepreview", { { "rules", nlohmann::json::array() } } },
			{ "inlinebot", { { "rules", nlohmann::json::array() } } },
			{ "disabled_remote", nlohmann::json::array() },
			{ "disabled_bundled", nlohmann::json::array() },
		};
	}
	return result;
}

void ResolveLocalInlineBots(
		not_null<Main::Session*> session,
		nlohmann::json rules,
		Fn<void(std::optional<nlohmann::json>, QString)> done) {
	struct State {
		not_null<Main::Session*> session;
		nlohmann::json rules;
		Fn<void(std::optional<nlohmann::json>, QString)> done;
		std::shared_ptr<Fn<void()>> next;
		size_t index = 0;
	};
	const auto state = std::make_shared<State>(State{
		.session = session,
		.rules = std::move(rules),
		.done = std::move(done),
	});
	state->next = std::make_shared<Fn<void()>>();
	const auto weakState = std::weak_ptr<State>(state);
	*state->next = [weakState] {
		const auto state = weakState.lock();
		if (!state) {
			return;
		}
		const auto section = state->rules.find("inlinebot");
		if (section == state->rules.end() || !section->is_object()) {
			state->done(std::nullopt, u"Invalid inlinebot section."_q);
			return;
		}
		const auto list = section->find("rules");
		if (list == section->end() || !list->is_array()) {
			state->done(std::nullopt, u"Invalid inlinebot rules."_q);
			return;
		}
		if (state->index >= list->size()) {
			state->done(std::move(state->rules), {});
			return;
		}
		const auto currentIndex = state->index++;
		auto &entry = (*list)[currentIndex];
		if (!entry.is_object()) {
			state->done(std::nullopt, u"Invalid inline bot rule."_q);
			return;
		}
		if (const auto enabled = entry.find("enabled");
			enabled != entry.end()
			&& enabled->is_boolean()
			&& !enabled->get<bool>()) {
			(*state->next)();
			return;
		}
		const auto usernameValue = entry.find("username");
		const auto idValue = entry.find("bot_id");
		if (usernameValue == entry.end() || !usernameValue->is_string()
			|| (idValue != entry.end()
				&& !idValue->is_number_unsigned()
				&& !(idValue->is_number_integer()
					&& idValue->get<int64>() >= 0))) {
			state->done(std::nullopt, u"Invalid inline bot identity."_q);
			return;
		}
		const auto rawUsername = usernameValue->get<std::string>();
		const auto username = QString::fromUtf8(
			rawUsername.data(),
			int(rawUsername.size()));
		const auto expectedId = (idValue == entry.end())
			? uint64(0)
			: idValue->get<uint64>();
		const auto weak = base::make_weak(state->session.get());
		state->session->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(username),
			MTP_string()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			if (!weak) {
				state->done(std::nullopt, u"Session closed while resolving bot."_q);
				return;
			}
			result.match([&](const MTPDcontacts_resolvedPeer &data) {
				state->session->data().processUsers(data.vusers());
				state->session->data().processChats(data.vchats());
				const auto peer = state->session->data().peerLoaded(
					peerFromMTP(data.vpeer()));
				const auto bot = peer ? peer->asUser() : nullptr;
				const auto bareId = bot ? peerToUser(bot->id).bare : uint64(0);
				if (!bot
					|| !bot->isBot()
					|| !bot->botInfo
					|| bot->botInfo->inlinePlaceholder.isEmpty()) {
					state->done(
						std::nullopt,
						u"@"_q + username + u" is not an inline bot."_q);
					return;
				} else if (expectedId && expectedId != bareId) {
					state->done(
						std::nullopt,
						u"@"_q + username + u" resolved to a different bot ID."_q);
					return;
				}
				state->rules["inlinebot"]["rules"][currentIndex]["bot_id"] = bareId;
				(*state->next)();
			});
		}).fail([=](const MTP::Error &error) {
			state->done(std::nullopt, error.type());
		}).send();
	};
	(*state->next)();
}

void ShowLinkRulesEditor(
		Window::SessionController *controller) {
	if (!controller) {
		return;
	}
	if (Ayu::RemoteMetadata::Configured()) {
		Ayu::RemoteMetadata::Refresh(&controller->session());
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::ayu_LinkRulesEditorTitle());
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::ayu_LinkRulesEditorDescription(tr::rich),
			st::boxLabel));
		const auto json = QByteArray::fromStdString(EditableRules().dump(2));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::ayu_LinkRulesEditorPlaceholder(),
			QString::fromUtf8(json)));
		field->setMinHeight(300);
		field->setMaxHeight(300);

		const auto test = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::SingleLine,
			tr::ayu_LinkRulesTestPlaceholder()));

		box->addButton(tr::lng_settings_save(), [=] {
			try {
				const auto bytes = field->getLastText().toUtf8();
				auto parsed = nlohmann::json::parse(
					bytes.constData(),
					bytes.constData() + bytes.size());
				const auto weakController = base::make_weak(controller);
				const auto weakBox = base::make_weak(box.get());
				ResolveLocalInlineBots(
					&controller->session(),
					std::move(parsed),
					[=](std::optional<nlohmann::json> resolved, QString error) {
						const auto strong = weakController.get();
						if (!strong || !weakBox) {
							return;
						}
						if (!resolved
							|| !Ayu::LinkRules::ValidateLocalSettings(*resolved)) {
							strong->showToast(error.isEmpty()
								? tr::ayu_LinkRulesInvalid(tr::now)
								: error);
							return;
						}
						AyuSettings::getInstance().setLinkRules(std::move(*resolved));
						strong->showToast(tr::lng_box_done(tr::now));
						weakBox->closeBox();
					});
			} catch (...) {
				controller->showToast(tr::ayu_LinkRulesInvalid(tr::now));
			}
		});
		box->addButton(tr::ayu_LinkRulesTestAction(), [=] {
			const auto input = test->getLastText();
			const auto preview = Ayu::LinkRules::RewritePreviewUrl(input);
			if (preview.changed) {
				controller->showToast(preview.previewUrl);
			} else if (const auto bot = Ayu::LinkRules::MatchInlineBot(input, true)) {
				controller->showToast(
					u"@"_q + bot->username + u" ← "_q + bot->query);
			} else {
				controller->showToast(tr::ayu_LinkRulesNoMatch(tr::now));
			}
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void BuildTranslator(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::lng_translate_settings_subtitle());

	auto *settings = &AyuSettings::getInstance();

	const auto options = std::vector{
		std::pair(TranslationProvider::Telegram, QString("Telegram")),
		std::pair(TranslationProvider::Google, QString("Google")),
		std::pair(TranslationProvider::Yandex, QString("Yandex")),
	};
	const auto nativeAvailable = Platform::IsTranslateProviderAvailable();
	auto availableOptions = options;
	if (nativeAvailable) {
		availableOptions.push_back(std::pair(
			TranslationProvider::Native,
			[] {
				if constexpr (Platform::IsMac()) {
					return QString("macOS");
				} else if constexpr (Platform::IsWindows()) {
					return QString("Windows");
				} else {
					return QString("Linux");
				}
			}()));
	}
	auto optionLabels = std::vector<QString>();
	optionLabels.reserve(availableOptions.size());
	for (const auto &option : availableOptions) {
		optionLabels.push_back(option.second);
	}

	const auto getIndex = [=](TranslationProvider val) {
		const auto i = ranges::find(
			availableOptions,
			val,
			&std::pair<TranslationProvider, QString>::first);
		return (i != end(availableOptions))
			? int(i - begin(availableOptions))
			: 0;
	};

	auto currentVal = AyuSettings::getInstance().translationProviderValue()
		| rpl::map(getIndex)
		| rpl::map([=](int val) { return availableOptions[val].second; });

	const auto button = builder.addButton({
		.id = u"ayu/translationProvider"_q,
		.title = tr::ayu_TranslationProvider(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			if (const auto controller = Core::App().activeWindow()->sessionController()) {
				controller->show(Box(
						[=](not_null<Ui::GenericBox*> box) {
							const auto save = [=](int index) {
								const auto option = availableOptions[index].first;
								AyuSettings::getInstance().setTranslationProvider(option);

								if constexpr (Platform::IsMac()) {
									if (option == TranslationProvider::Native) {
										controller->showToast(Ui::Toast::Config{
											.text = tr::lng_translate_settings_use_platform_mac_about(tr::now, tr::rich),
											.duration = 6 * crl::time(1000)
										});
									}
								}
							};
							SingleChoiceBox(box, {
								.title = tr::ayu_TranslationProvider(),
								.options = optionLabels,
								.initialSelection = getIndex(settings->translationProvider()),
								.callback = save,
							});
						}));
			}
		},
	});
	if (button) {
		ayu.addBetaBadge(button);
	}
}

void BuildShowPeerId(SectionBuilder &builder) {
	auto *settings = &AyuSettings::getInstance();

	const auto options = std::vector{
		QString(tr::ayu_SettingsShowID_Hide(tr::now)),
		QString("Telegram API"),
		QString("Bot API")
	};

	auto currentVal = AyuSettings::getInstance().showPeerIdValue()
		| rpl::map([=](PeerIdDisplay val) {
			return options[static_cast<int>(val)];
		});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"ayu/showPeerId"_q,
		.altIds = { u"ayu/showIdAndDc"_q },
		.title = tr::ayu_SettingsShowID(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						AyuSettings::getInstance().setShowPeerId(
							static_cast<PeerIdDisplay>(index));
					};
					SingleChoiceBox(box, {
						.title = tr::ayu_SettingsShowID(),
						.options = options,
						.initialSelection = static_cast<int>(settings->showPeerId()),
						.callback = save,
					});
				}));
		},
	});
}

void BuildQoLToggles(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	BuildTranslator(builder, ayu);
	ayu.addSectionDivider();

	builder.addSubsectionTitle(tr::ayu_CategoryGeneral());

	const auto controller = builder.controller();
	ayu.addToggle({
		.id = u"ayu/disableStories"_q,
		.altIds = { u"ayu/hideStories"_q },
		.title = tr::ayu_DisableStories(),
		.getter = [=] { return settings->disableStories(); },
		.setter = [=](bool enabled) {
			AyuSettings::getInstance().setDisableStories(enabled);
			ShowRestartPrompt(controller);
		},
	});
	ayu.addSettingToggle({
		.id = u"ayu/openCommunityOnlyFromBadge"_q,
		.title = tr::ayu_OpenCommunityOnlyFromBadge(),
		.getter = &AyuSettings::openCommunityOnlyFromBadge,
		.setter = &AyuSettings::setOpenCommunityOnlyFromBadge,
	});
	builder.addDividerText(tr::ayu_OpenCommunityOnlyFromBadgeDescription());

	ayu.addSettingToggle({
		.id = u"ayu/disableOpenLinkWarning"_q,
		.title = tr::ayu_DisableOpenLinkWarning(),
		.getter = &AyuSettings::disableOpenLinkWarning,
		.setter = &AyuSettings::setDisableOpenLinkWarning,
	});

	ayu.addCollapsibleToggle({
		.id = u"ayu/similarChannels"_q,
		.title = tr::ayu_DisableSimilarChannels(),
		.checkboxes = {
			NestedEntry{
				tr::ayu_CollapseSimilarChannels(tr::now),
				[] { return AyuSettings::getInstance().collapseSimilarChannels(); },
				[](bool v) { AyuSettings::getInstance().setCollapseSimilarChannels(v); }
			},
			NestedEntry{
				tr::ayu_HideSimilarChannelsTab(tr::now),
				[] { return AyuSettings::getInstance().hideSimilarChannels(); },
				[](bool v) { AyuSettings::getInstance().setHideSimilarChannels(v); }
			}
		},
		.toggledWhenAll = true,
	});

	ayu.addSettingToggle({
		.id = u"ayu/disableNotificationsDelay"_q,
		.title = tr::ayu_DisableNotificationsDelay(),
		.getter = &AyuSettings::disableNotificationsDelay,
		.setter = &AyuSettings::setDisableNotificationsDelay,
	});

	ayu.addSectionDivider();

	const auto zalgoButton = builder.addButton({
		.id = u"ayu/filterZalgo"_q,
		.title = tr::ayu_FilterZalgo(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filterZalgo()),
	});
	if (zalgoButton) {
		zalgoButton->toggledValue(
		) | rpl::filter(
			[=](bool enabled) {
				return (enabled != settings->filterZalgo());
			}
		) | on_next(
			[=](bool enabled) {
				AyuSettings::getInstance().setFilterZalgo(enabled);
				ShowRestartPrompt(controller);
			},
			zalgoButton->lifetime());
		ayu.addBetaBadge(zalgoButton);
	}

	ayu.addSettingToggle({
		.id = u"ayu/improveLinkPreviews"_q,
		.title = tr::ayu_ImproveLinkPreviews(),
		.getter = &AyuSettings::improveLinkPreviews,
		.setter = &AyuSettings::setImproveLinkPreviews,
	});
	builder.addButton({
		.id = u"ayu/linkRules"_q,
		.title = tr::ayu_LinkRules(),
		.icon = { &st::menuIconLink },
		.onClick = [=] { ShowLinkRulesEditor(controller); },
	});
	builder.addButton({
		.id = u"ayu/linkRulesRefresh"_q,
		.title = tr::ayu_LinkRulesRefresh(),
		.icon = { &st::menuIconRestore },
		.label = rpl::single(Ayu::RemoteMetadata::Configured()
			? tr::ayu_LinkRulesRevision(
				tr::now,
				lt_count,
				int(Ayu::LinkRules::RemoteRevision()))
			: tr::ayu_LinkRulesUnconfigured(tr::now)),
		.onClick = [=] {
			if (!controller) {
				return;
			}
			const auto weak = base::make_weak(controller);
			Ayu::RemoteMetadata::Refresh(
				&controller->session(),
				[=](bool success, QString error) {
					if (const auto strong = weak.get()) {
						const auto invalid = Ayu::LinkRules::InvalidRemoteRules();
						strong->showToast(success
							? (!invalid
								? tr::ayu_LinkRulesRefreshDone(tr::now)
								: tr::ayu_LinkRulesInvalidRemote(
									tr::now,
									lt_count,
									invalid))
							: (error.isEmpty()
							? tr::ayu_LinkRulesRefreshFailed(tr::now)
							: error));
					}
				});
		},
	});
	const auto inlineQueries = builder.addButton({
		.id = u"ayu/autoInlineBotQueries"_q,
		.title = tr::ayu_AutoInlineBotQueries(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->autoInlineBotQueriesValue(),
		.keywords = { u"inline"_q, u"bot"_q, u"url"_q },
	});
	if (inlineQueries) {
		inlineQueries->toggledChanges(
		) | rpl::on_next([=](bool enabled) {
			if (enabled == settings->autoInlineBotQueries()) {
				return;
			} else if (!enabled) {
				settings->setAutoInlineBotQueries(false);
				return;
			} else if (settings->inlineBotConsent()) {
				settings->setAutoInlineBotQueries(true);
				return;
			}
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_AutoInlineBotPrivacyWarning(),
				.confirmed = [=](Fn<void()> close) {
					settings->setInlineBotConsent(true);
					settings->setAutoInlineBotQueries(true);
					close();
				},
				.cancelled = [=](Fn<void()> close) {
					settings->setAutoInlineBotQueries(false);
					close();
				},
				.confirmText = tr::ayu_AutoInlineBotEnableAction(),
			}));
		}, inlineQueries->lifetime());
	}
	builder.addDividerText(rpl::combine(
		settings->autoInlineBotQueriesValue(),
		settings->inlineBotConsentValue()
	) | rpl::map([](bool enabled, bool consent) {
		return (enabled && !consent)
			? tr::ayu_AutoInlineBotWaitingConsent(tr::now)
			: tr::ayu_AutoInlineBotQueriesDescription(tr::now);
	}));
	ayu.addCollapsibleToggle({
		.id = u"ayu/confirmations"_q,
		.title = tr::ayu_ConfirmationsTitle(),
		.checkboxes = {
			NestedEntry{
				tr::ayu_StickerConfirmation(tr::now),
				[] { return AyuSettings::getInstance().stickerConfirmation(); },
				[](bool v) { AyuSettings::getInstance().setStickerConfirmation(v); }
			},
			NestedEntry{
				tr::ayu_GIFConfirmation(tr::now),
				[] { return AyuSettings::getInstance().gifConfirmation(); },
				[](bool v) { AyuSettings::getInstance().setGifConfirmation(v); }
			},
			NestedEntry{
				tr::ayu_VoiceConfirmation(tr::now),
				[] { return AyuSettings::getInstance().voiceConfirmation(); },
				[](bool v) { AyuSettings::getInstance().setVoiceConfirmation(v); }
			},
			NestedEntry{
				tr::ayu_RoundConfirmation(tr::now),
				[] { return AyuSettings::getInstance().roundConfirmation(); },
				[](bool v) { AyuSettings::getInstance().setRoundConfirmation(v); }
			}
		},
		.toggledWhenAll = false,
	});
	ayu.addSettingToggle({
		.id = u"ayu/showMessageSeconds"_q,
		.altIds = { u"ayu/formatTimeWithSeconds"_q },
		.title = tr::ayu_SettingsShowMessageSeconds(),
		.getter = &AyuSettings::showMessageSeconds,
		.setter = &AyuSettings::setShowMessageSeconds,
	});
	ayu.addSettingToggle({
		.id = u"ayu/showMessageId"_q,
		.title = tr::ayu_SettingsShowMessageId(),
		.getter = &AyuSettings::showMessageId,
		.setter = &AyuSettings::setShowMessageId,
	});

	BuildShowPeerId(builder);

	ayu.addSectionDivider();

	builder.addSubsectionTitle(rpl::single(QString("Webview")));

	ayu.addSettingToggle({
		.id = u"ayu/spoofWebviewAsAndroid"_q,
		.title = tr::ayu_SettingsSpoofWebviewAsAndroid(),
		.getter = &AyuSettings::spoofWebviewAsAndroid,
		.setter = &AyuSettings::setSpoofWebviewAsAndroid,
	});

	ayu.addCollapsibleToggle({
		.id = u"ayu/biggerWindow"_q,
		.title = tr::ayu_SettingsBiggerWindow(),
		.checkboxes = {
			NestedEntry{
				tr::ayu_SettingsIncreaseWebviewHeight(tr::now),
				[] { return AyuSettings::getInstance().increaseWebviewHeight(); },
				[](bool v) { AyuSettings::getInstance().setIncreaseWebviewHeight(v); }
			},
			NestedEntry{
				tr::ayu_SettingsIncreaseWebviewWidth(tr::now),
				[] { return AyuSettings::getInstance().increaseWebviewWidth(); },
				[](bool v) { AyuSettings::getInstance().setIncreaseWebviewWidth(v); }
			}
		},
		.toggledWhenAll = false,
	});
}

const auto kMeta = BuildHelper({
	.id = AyuGeneral::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryGeneral,
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildQoLToggles(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuGeneral::title() {
	return tr::ayu_CategoryGeneral();
}

AyuGeneral::AyuGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuGeneral::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuGeneralId() {
	return AyuGeneral::Id();
}

} // namespace Settings
