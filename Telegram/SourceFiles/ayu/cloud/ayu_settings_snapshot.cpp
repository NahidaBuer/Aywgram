#include "ayu/cloud/ayu_settings_snapshot.h"

#include "ayu/ayu_account_settings.h"
#include "ayu/ayu_chat_settings.h"
#include "ayu/ayu_settings.h"
#include "ayu/cloud/ayu_cloud_codec.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"
#include "core/version.h"
#include "data/data_auto_download.h"
#include "data/data_cloud_themes.h"
#include "data/data_session.h"
#include "dialogs/ui/dialogs_quick_action.h"
#include "history/view/history_view_quick_action.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "spellcheck/spellcheck_types.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "ui/widgets/chat_filters_tabs_mode.h"
#include "ui/widgets/fields/input_field.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"

#include <QtGui/QFontDatabase>

#include <optional>

namespace AyuCloud {
namespace {

using Json = nlohmann::json;

bool Has(uint32_t mask, Category category) {
	return (mask & uint32_t(category)) != 0;
}

QString FromString(const std::string &value) {
	return QString::fromUtf8(value.data(), int(value.size()));
}

std::string ToString(const QString &value) {
	const auto utf8 = value.toUtf8();
	return std::string(utf8.constData(), utf8.size());
}

template <typename Type>
Type Read(const Json &data, const char *key, Type fallback) {
	const auto i = data.find(key);
	if (i == data.end()) {
		return fallback;
	}
	try {
		return i->get<Type>();
	} catch (...) {
		return fallback;
	}
}

QString ReadString(
		const Json &data,
		const char *key,
		QString fallback = {}) {
	const auto i = data.find(key);
	return (i != data.end() && i->is_string())
		? FromString(i->get<std::string>())
		: fallback;
}

struct PortableProxySettings {
	MTP::ProxyData::Settings mode = MTP::ProxyData::Settings::System;
	MTP::ProxyData selected;
	std::vector<MTP::ProxyData> list;
	bool tryIPv6 = false;
	bool useForCalls = false;
	bool rotationEnabled = false;
	int rotationTimeout = Core::SettingsProxy::kDefaultProxyRotationTimeout;
	std::vector<int> preferredIndices;
};

int ProxyTypeValue(MTP::ProxyData::Type type) {
	switch (type) {
	case MTP::ProxyData::Type::Socks5: return 1;
	case MTP::ProxyData::Type::Http: return 2;
	case MTP::ProxyData::Type::Mtproto: return 3;
	case MTP::ProxyData::Type::Web: return 4;
	case MTP::ProxyData::Type::None: return 0;
	}
	Unexpected("Unknown proxy type.");
}

std::optional<MTP::ProxyData::Type> ProxyTypeFromValue(int value) {
	switch (value) {
	case 1: return MTP::ProxyData::Type::Socks5;
	case 2: return MTP::ProxyData::Type::Http;
	case 3: return MTP::ProxyData::Type::Mtproto;
	case 4: return MTP::ProxyData::Type::Web;
	}
	return std::nullopt;
}

int ProxyModeValue(MTP::ProxyData::Settings mode) {
	switch (mode) {
	case MTP::ProxyData::Settings::System: return 0;
	case MTP::ProxyData::Settings::Enabled: return 1;
	case MTP::ProxyData::Settings::Disabled: return 2;
	}
	Unexpected("Unknown proxy mode.");
}

std::optional<MTP::ProxyData::Settings> ProxyModeFromValue(int value) {
	switch (value) {
	case 0: return MTP::ProxyData::Settings::System;
	case 1: return MTP::ProxyData::Settings::Enabled;
	case 2: return MTP::ProxyData::Settings::Disabled;
	}
	return std::nullopt;
}

Json ExportProxy(const MTP::ProxyData &proxy) {
	if (proxy.type == MTP::ProxyData::Type::None) {
		return nullptr;
	}
	return Json{
		{ "type", ProxyTypeValue(proxy.type) },
		{ "host", ToString(proxy.host) },
		{ "port", proxy.port },
		{ "username", ToString(proxy.user) },
		{ "password", ToString(proxy.password) },
	};
}

std::optional<MTP::ProxyData> ParseProxy(const Json &data, bool allowNone) {
	if (data.is_null() && allowNone) {
		return MTP::ProxyData();
	}
	if (!data.is_object()
		|| !data.contains("type")
		|| !data.at("type").is_number_integer()
		|| !data.contains("host")
		|| !data.at("host").is_string()
		|| !data.contains("port")
		|| !data.at("port").is_number_unsigned()
		|| !data.contains("username")
		|| !data.at("username").is_string()
		|| !data.contains("password")
		|| !data.at("password").is_string()) {
		return std::nullopt;
	}
	const auto type = ProxyTypeFromValue(Read(data, "type", 0));
	const auto host = ReadString(data, "host");
	const auto port = Read(data, "port", uint32_t(0));
	const auto user = ReadString(data, "username");
	const auto password = ReadString(data, "password");
	if (!type
		|| host.isEmpty()
		|| host.size() > 2048
		|| !port
		|| port > 65535
		|| user.size() > 2048
		|| password.size() > 4096) {
		return std::nullopt;
	}
	const auto result = MTP::ProxyData{
		.type = *type,
		.host = host,
		.port = port,
		.user = user,
		.password = password,
	};
	return result.valid() ? std::optional(result) : std::nullopt;
}

Json ExportProxySettings() {
	const auto &proxy = Core::App().settings().proxy();
	auto list = Json::array();
	for (const auto &entry : proxy.list()) {
		list.push_back(ExportProxy(entry));
	}
	auto preferred = Json::array();
	for (const auto index : proxy.proxyRotationPreferredIndices()) {
		preferred.push_back(index);
	}
	return Json{
		{ "mode", ProxyModeValue(proxy.settings()) },
		{ "selected", ExportProxy(proxy.selected()) },
		{ "list", std::move(list) },
		{ "try_ipv6", proxy.tryIPv6() },
		{ "use_for_calls", proxy.useProxyForCalls() },
		{ "rotation_enabled", proxy.proxyRotationEnabled() },
		{ "rotation_timeout", proxy.proxyRotationTimeout() },
		{ "preferred_indices", std::move(preferred) },
	};
}

std::optional<PortableProxySettings> ParseProxySettings(const Json &data) {
	if (!data.is_object()
		|| !data.contains("mode")
		|| !data.at("mode").is_number_integer()
		|| !data.contains("selected")
		|| !data.contains("try_ipv6")
		|| !data.at("try_ipv6").is_boolean()
		|| !data.contains("use_for_calls")
		|| !data.at("use_for_calls").is_boolean()
		|| !data.contains("rotation_enabled")
		|| !data.at("rotation_enabled").is_boolean()
		|| !data.contains("rotation_timeout")
		|| !data.at("rotation_timeout").is_number_integer()) {
		return std::nullopt;
	}
	const auto mode = ProxyModeFromValue(Read(data, "mode", -1));
	const auto selected = ParseProxy(data.value("selected", Json()), true);
	const auto entries = data.find("list");
	const auto preferred = data.find("preferred_indices");
	if (!mode
		|| !selected
		|| entries == data.end()
		|| !entries->is_array()
		|| entries->size() > 256
		|| preferred == data.end()
		|| !preferred->is_array()
		|| preferred->size() > entries->size()) {
		return std::nullopt;
	}
	auto result = PortableProxySettings{
		.mode = *mode,
		.selected = *selected,
		.tryIPv6 = Read(data, "try_ipv6", false),
		.useForCalls = Read(data, "use_for_calls", false),
		.rotationEnabled = Read(data, "rotation_enabled", false),
		.rotationTimeout = Read(data, "rotation_timeout", 0),
	};
	if (!ranges::contains(
			Core::SettingsProxy::kProxyRotationTimeouts,
			result.rotationTimeout)) {
		return std::nullopt;
	}
	for (const auto &entry : *entries) {
		const auto parsed = ParseProxy(entry, false);
		if (!parsed) {
			return std::nullopt;
		}
		result.list.push_back(*parsed);
	}
	for (const auto &entry : *preferred) {
		if (!entry.is_number_integer()) {
			return std::nullopt;
		}
		const auto index = entry.get<int>();
		if (index < 0
			|| index >= int(result.list.size())
			|| ranges::contains(result.preferredIndices, index)) {
			return std::nullopt;
		}
		result.preferredIndices.push_back(index);
	}
	if (result.mode == MTP::ProxyData::Settings::Enabled
		&& !result.selected.valid()) {
		return std::nullopt;
	}
	return result;
}

void ApplyProxySettings(const PortableProxySettings &data) {
	auto &proxy = Core::App().settings().proxy();
	proxy.setList(data.list);
	proxy.setTryIPv6(data.tryIPv6);
	proxy.setUseProxyForCalls(data.useForCalls);
	proxy.setProxyRotationEnabled(data.rotationEnabled);
	proxy.setProxyRotationTimeout(data.rotationTimeout);
	proxy.setProxyRotationPreferredIndices(data.preferredIndices);
	Core::App().setCurrentProxy(data.selected, data.mode);
}

Json ExportTelegramGlobal() {
	const auto &s = Core::App().settings();
	const auto title = s.windowTitleContent();
	const auto quality = s.videoQuality();
	auto dictionaries = Json::array();
	for (const auto value : s.dictionariesEnabled()) {
		dictionaries.push_back(value);
	}
	auto skipLanguages = Json::array();
	for (const auto value : s.skipTranslationLanguages()) {
		skipLanguages.push_back(int(value.value));
	}
	return Json{
		{ "adaptive_for_wide", s.adaptiveForWide() },
		{ "moderate_mode", s.moderateModeEnabled() },
		{ "song_volume", s.songVolume() },
		{ "video_volume", s.videoVolume() },
		{ "sound_notify", s.soundNotify() },
		{ "desktop_notify", s.desktopNotify() },
		{ "flash_bounce_notify", s.flashBounceNotify() },
		{ "notify_view", int(s.notifyView()) },
		{ "native_notifications", s.nativeNotifications() },
		{ "skip_toasts_in_focus", s.skipToastsInFocus() },
		{ "notifications_count", s.notificationsCount() },
		{ "notifications_corner", int(s.notificationsCorner()) },
		{ "notifications_volume", s.notificationsVolume() },
		{ "include_muted_counter", s.includeMutedCounter() },
		{ "include_muted_counter_folders", s.includeMutedCounterFolders() },
		{ "count_unread_messages", s.countUnreadMessages() },
		{ "notify_about_pinned", s.notifyAboutPinned() },
		{ "send_files_way", s.sendFilesWay().serialize() },
		{ "send_submit_way", int(s.sendSubmitWay()) },
		{ "loop_animated_stickers", s.loopAnimatedStickers() },
		{ "large_emoji", s.largeEmoji() },
		{ "replace_emoji", s.replaceEmoji() },
		{ "system_text_replace", s.systemTextReplace() },
		{ "suggest_emoji", s.suggestEmoji() },
		{ "suggest_stickers", s.suggestStickersByEmoji() },
		{ "suggest_animated_emoji", s.suggestAnimatedEmoji() },
		{ "corner_reaction", s.cornerReaction() },
		{ "corner_reply", s.cornerReply() },
		{ "pull_to_next_channel", s.pullToNextChannel() },
		{ "spellchecker", s.spellcheckerEnabled() },
		{ "dictionaries", std::move(dictionaries) },
		{ "auto_download_dictionaries", s.autoDownloadDictionaries() },
		{ "main_menu_accounts", s.mainMenuAccountsShown() },
		{ "video_speed", s.videoPlaybackSpeed(true) },
		{ "voice_speed", s.voicePlaybackSpeed(true) },
		{ "audio_speed", s.audioPlaybackSpeed(true) },
		{ "player_repeat", int(s.playerRepeatMode()) },
		{ "player_order", int(s.playerOrderMode()) },
		{ "chat_quick_action", int(s.chatQuickAction()) },
		{ "quick_dialog_action", int(s.quickDialogAction()) },
		{ "translate_button", s.translateButtonEnabled() },
		{ "platform_translation", s.usePlatformTranslation() },
		{ "translate_chat", s.translateChatEnabled() },
		{ "translate_to", int(s.translateTo().value) },
		{ "skip_translation_languages", std::move(skipLanguages) },
		{ "remember_delete_for_me", s.rememberedDeleteMessageOnlyForYou() },
		{ "window_title_hide_chat", title.hideChatName },
		{ "window_title_hide_account", title.hideAccountName },
		{ "window_title_hide_unread", title.hideTotalUnread },
		{ "record_video_messages", s.recordVideoMessages() },
		{ "video_quality_manual", bool(quality.manual) },
		{ "video_quality_height", quality.height },
		{ "video_quality_original", bool(quality.original) },
		{ "weather_celsius", s.weatherInCelsius().value_or(true) },
		{ "iv_zoom", s.ivZoom() },
		{ "media_grid_zoom", s.mediaGridZoomStep() },
		{ "chat_filters_horizontal", s.chatFiltersHorizontal() },
		{ "chat_filters_tabs", int(s.chatFiltersTabsMode()) },
		{ "call_output_volume", s.callOutputVolume() },
		{ "call_input_volume", s.callInputVolume() },
		{ "call_ducking", s.callAudioDuckingEnabled() },
		{ "group_call_ptt", s.groupCallPushToTalk() },
		{ "group_call_ptt_delay", s.groupCallPushToTalkDelay() },
		{ "group_call_noise_suppression", s.groupCallNoiseSuppression() },
		{ "notify_from_all", s.notifyFromAll() },
	};
}

void ApplyTelegramGlobal(const Json &data) {
	auto &s = Core::App().settings();
	s.setAdaptiveForWide(Read(data, "adaptive_for_wide", s.adaptiveForWide()));
	s.setModerateModeEnabled(Read(data, "moderate_mode", s.moderateModeEnabled()));
	s.setSongVolume(std::clamp(Read(data, "song_volume", s.songVolume()), 0., 1.));
	s.setVideoVolume(std::clamp(Read(data, "video_volume", s.videoVolume()), 0., 1.));
	s.setSoundNotify(Read(data, "sound_notify", s.soundNotify()));
	s.setDesktopNotify(Read(data, "desktop_notify", s.desktopNotify()));
	s.setFlashBounceNotify(Read(data, "flash_bounce_notify", s.flashBounceNotify()));
	s.setNotifyView(Core::Settings::NotifyView(std::clamp(
		Read(data, "notify_view", int(s.notifyView())), 0, 2)));
	s.setNativeNotifications(Read(data, "native_notifications", s.nativeNotifications()));
	s.setSkipToastsInFocus(Read(data, "skip_toasts_in_focus", s.skipToastsInFocus()));
	s.setNotificationsCount(std::clamp(
		Read(data, "notifications_count", s.notificationsCount()), 1, 5));
	s.setNotificationsCorner(Core::Settings::ScreenCorner(std::clamp(
		Read(data, "notifications_corner", int(s.notificationsCorner())), 0, 4)));
	s.setNotificationsVolume(ushort(std::clamp(
		Read(data, "notifications_volume", int(s.notificationsVolume())), 0, 100)));
	s.setIncludeMutedCounter(Read(data, "include_muted_counter", s.includeMutedCounter()));
	s.setIncludeMutedCounterFolders(Read(data, "include_muted_counter_folders", s.includeMutedCounterFolders()));
	s.setCountUnreadMessages(Read(data, "count_unread_messages", s.countUnreadMessages()));
	s.setNotifyAboutPinned(Read(data, "notify_about_pinned", s.notifyAboutPinned()));
	if (const auto way = Ui::SendFilesWay::FromSerialized(
		Read(data, "send_files_way", s.sendFilesWay().serialize()))) {
		s.setSendFilesWay(*way);
	}
	s.setSendSubmitWay(Ui::InputSubmitSettings(std::clamp(
		Read(data, "send_submit_way", int(s.sendSubmitWay())), 0, 3)));
	s.setLoopAnimatedStickers(Read(data, "loop_animated_stickers", s.loopAnimatedStickers()));
	s.setLargeEmoji(Read(data, "large_emoji", s.largeEmoji()));
	s.setReplaceEmoji(Read(data, "replace_emoji", s.replaceEmoji()));
	s.setSystemTextReplace(Read(data, "system_text_replace", s.systemTextReplace()));
	s.setSuggestEmoji(Read(data, "suggest_emoji", s.suggestEmoji()));
	s.setSuggestStickersByEmoji(Read(data, "suggest_stickers", s.suggestStickersByEmoji()));
	s.setSuggestAnimatedEmoji(Read(data, "suggest_animated_emoji", s.suggestAnimatedEmoji()));
	s.setCornerReaction(Read(data, "corner_reaction", s.cornerReaction()));
	s.setCornerReply(Read(data, "corner_reply", s.cornerReply()));
	s.setPullToNextChannel(Read(data, "pull_to_next_channel", s.pullToNextChannel()));
	s.setSpellcheckerEnabled(Read(data, "spellchecker", s.spellcheckerEnabled()));
	if (const auto i = data.find("dictionaries"); i != data.end() && i->is_array()) {
		auto values = std::vector<int>();
		for (const auto &value : *i) {
			if (value.is_number_integer()) {
				values.push_back(value.get<int>());
			}
		}
		s.setDictionariesEnabled(std::move(values));
	}
	s.setAutoDownloadDictionaries(Read(data, "auto_download_dictionaries", s.autoDownloadDictionaries()));
	s.setMainMenuAccountsShown(Read(data, "main_menu_accounts", s.mainMenuAccountsShown()));
	s.setVideoPlaybackSpeed(std::clamp(Read(data, "video_speed", s.videoPlaybackSpeed(true)), Media::kSpeedMin, Media::kSpeedMax));
	s.setVoicePlaybackSpeed(std::clamp(Read(data, "voice_speed", s.voicePlaybackSpeed(true)), Media::kSpeedMin, Media::kSpeedMax));
	s.setAudioPlaybackSpeed(std::clamp(Read(data, "audio_speed", s.audioPlaybackSpeed(true)), Media::kSpeedMin, Media::kSpeedMax));
	s.setPlayerRepeatMode(Media::RepeatMode(std::clamp(Read(data, "player_repeat", int(s.playerRepeatMode())), 0, 2)));
	s.setPlayerOrderMode(Media::OrderMode(std::clamp(Read(data, "player_order", int(s.playerOrderMode())), 0, 2)));
	s.setChatQuickAction(HistoryView::DoubleClickQuickAction(std::clamp(Read(data, "chat_quick_action", int(s.chatQuickAction())), 0, 2)));
	s.setQuickDialogAction(Dialogs::Ui::QuickDialogAction(std::clamp(Read(data, "quick_dialog_action", int(s.quickDialogAction())), 0, 5)));
	s.setTranslateButtonEnabled(Read(data, "translate_button", s.translateButtonEnabled()));
	s.setUsePlatformTranslation(Read(data, "platform_translation", s.usePlatformTranslation()));
	s.setTranslateChatEnabled(Read(data, "translate_chat", s.translateChatEnabled()));
	s.setTranslateTo(LanguageId{ QLocale::Language(Read(data, "translate_to", int(s.translateTo().value))) });
	if (const auto i = data.find("skip_translation_languages"); i != data.end() && i->is_array()) {
		auto values = std::vector<LanguageId>();
		for (const auto &value : *i) {
			if (value.is_number_integer()) {
				values.push_back({ QLocale::Language(value.get<int>()) });
			}
		}
		s.setSkipTranslationLanguages(std::move(values));
	}
	s.setRememberedDeleteMessageOnlyForYou(Read(data, "remember_delete_for_me", s.rememberedDeleteMessageOnlyForYou()));
	s.setWindowTitleContent({
		.hideChatName = Read(data, "window_title_hide_chat", s.windowTitleContent().hideChatName),
		.hideAccountName = Read(data, "window_title_hide_account", s.windowTitleContent().hideAccountName),
		.hideTotalUnread = Read(data, "window_title_hide_unread", s.windowTitleContent().hideTotalUnread),
	});
	s.setRecordVideoMessages(Read(data, "record_video_messages", s.recordVideoMessages()));
	s.setVideoQuality({
		.manual = uint32(Read(data, "video_quality_manual", bool(s.videoQuality().manual))),
		.height = uint32(std::clamp(Read(data, "video_quality_height", int(s.videoQuality().height)), 0, 4320)),
		.original = uint32(Read(data, "video_quality_original", bool(s.videoQuality().original))),
	});
	s.setWeatherInCelsius(Read(data, "weather_celsius", s.weatherInCelsius().value_or(true)));
	s.setIvZoom(Read(data, "iv_zoom", s.ivZoom()));
	s.setMediaGridZoomStep(std::clamp(Read(data, "media_grid_zoom", s.mediaGridZoomStep()), -4, 4));
	s.setChatFiltersHorizontal(Read(data, "chat_filters_horizontal", s.chatFiltersHorizontal()));
	s.setChatFiltersTabsMode(Ui::ChatsFiltersTabsMode(std::clamp(Read(data, "chat_filters_tabs", int(s.chatFiltersTabsMode())), 0, 3)));
	s.setCallOutputVolume(std::clamp(Read(data, "call_output_volume", s.callOutputVolume()), 0, 100));
	s.setCallInputVolume(std::clamp(Read(data, "call_input_volume", s.callInputVolume()), 0, 100));
	s.setCallAudioDuckingEnabled(Read(data, "call_ducking", s.callAudioDuckingEnabled()));
	s.setGroupCallPushToTalk(Read(data, "group_call_ptt", s.groupCallPushToTalk()));
	s.setGroupCallPushToTalkDelay(std::clamp<crl::time>(Read(data, "group_call_ptt_delay", s.groupCallPushToTalkDelay()), 0, 1000));
	s.setGroupCallNoiseSuppression(Read(data, "group_call_noise_suppression", s.groupCallNoiseSuppression()));
	s.setNotifyFromAll(Read(data, "notify_from_all", s.notifyFromAll()));
}

Json ExportAccount(not_null<Main::Session*> session) {
	const auto &settings = session->settings();
	auto limits = Json::array();
	for (auto source = 0; source != Data::AutoDownload::kSourcesCount; ++source) {
		auto row = Json::array();
		for (auto type = 0; type != Data::AutoDownload::kTypesCount; ++type) {
			row.push_back(settings.autoDownload().bytesLimit(
				Data::AutoDownload::Source(source),
				Data::AutoDownload::Type(type)));
		}
		limits.push_back(std::move(row));
	}
	auto ringtone = Json::array();
	for (auto type = 0; type != 3; ++type) {
		ringtone.push_back(settings.ringtoneVolume(Data::DefaultNotify(type)));
	}
	auto reactions = Json::array();
	for (const auto &reaction : settings.extraFavoriteReactions()) {
		reactions.push_back(reaction.custom()
			? Json{ { "custom", std::to_string(reaction.custom()) } }
			: Json{ { "emoji", ToString(reaction.emoji()) } });
	}
	return Json{
		{ "ayu_account", AyuAccountSettings::CloudExport(session) },
		{ "ayu_chat_overrides", AyuChatSettings::CloudExport(session) },
		{ "ayu_ghost", AyuSettings::CloudExportAccount(session->userId().bare) },
		{ "auto_download", std::move(limits) },
		{ "archive_collapsed", settings.archiveCollapsed() },
		{ "archive_in_main_menu", settings.archiveInMainMenu() },
		{ "dialogs_filters_enabled", settings.dialogsFiltersEnabled() },
		{ "ringtone_default_volumes", std::move(ringtone) },
		{ "extra_favorite_reactions", std::move(reactions) },
	};
}

bool ValidateAccount(not_null<Main::Session*> session, const Json &data) {
	if (!data.is_object()) {
		return false;
	}
	const auto ayuAccount = data.find("ayu_account");
	const auto chatOverrides = data.find("ayu_chat_overrides");
	const auto ghost = data.find("ayu_ghost");
	if (ayuAccount == data.end()
		|| chatOverrides == data.end()
		|| ghost == data.end()
		|| !AyuAccountSettings::CloudValidate(*ayuAccount)
		|| !AyuChatSettings::CloudValidate(*chatOverrides)
		|| !AyuSettings::CloudValidateAccount(
			session->userId().bare,
			*ghost)) {
		return false;
	}
	return true;
}

bool ApplyAccount(not_null<Main::Session*> session, const Json &data) {
	if (!ValidateAccount(session, data)) {
		return false;
	}
	auto success = true;
	if (const auto value = data.find("ayu_account"); value != data.end()) {
		success = AyuAccountSettings::CloudApply(session, *value) && success;
	} else {
		success = false;
	}
	if (const auto value = data.find("ayu_chat_overrides"); value != data.end()) {
		success = AyuChatSettings::CloudApply(session, *value) && success;
	} else {
		success = false;
	}
	if (const auto value = data.find("ayu_ghost"); value != data.end()) {
		success = AyuSettings::CloudApplyAccount(
			session->userId().bare,
			*value) && success;
	} else {
		success = false;
	}
	auto &settings = session->settings();
	if (const auto value = data.find("auto_download"); value != data.end() && value->is_array() && value->size() == Data::AutoDownload::kSourcesCount) {
		for (auto source = 0; source != Data::AutoDownload::kSourcesCount; ++source) {
			const auto &row = (*value)[source];
			if (!row.is_array() || row.size() != Data::AutoDownload::kTypesCount) {
				continue;
			}
			for (auto type = 0; type != Data::AutoDownload::kTypesCount; ++type) {
				if (row[type].is_number_integer()) {
					const auto limit = row[type].get<int64>();
					if (limit >= 0 && limit <= Data::AutoDownload::kMaxBytesLimit) {
						settings.autoDownload().setBytesLimit(
							Data::AutoDownload::Source(source),
							Data::AutoDownload::Type(type),
							limit);
					}
				}
			}
		}
	}
	settings.setArchiveCollapsed(Read(data, "archive_collapsed", settings.archiveCollapsed()));
	settings.setArchiveInMainMenu(Read(data, "archive_in_main_menu", settings.archiveInMainMenu()));
	settings.setDialogsFiltersEnabled(Read(data, "dialogs_filters_enabled", settings.dialogsFiltersEnabled()));
	if (const auto value = data.find("ringtone_default_volumes"); value != data.end() && value->is_array()) {
		for (auto type = 0; type != std::min(3, int(value->size())); ++type) {
			if ((*value)[type].is_number_integer()) {
				settings.setRingtoneVolume(
					Data::DefaultNotify(type),
					ushort(std::clamp((*value)[type].get<int>(), 0, 100)));
			}
		}
	}
	if (const auto value = data.find("extra_favorite_reactions"); value != data.end() && value->is_array() && value->size() <= 100) {
		auto reactions = std::vector<Data::ReactionId>();
		for (const auto &entry : *value) {
			if (!entry.is_object()) {
				continue;
			}
			const auto emoji = ReadString(entry, "emoji");
			const auto custom = ReadString(entry, "custom").toULongLong();
			if (!emoji.isEmpty()) {
				reactions.push_back({ emoji });
			} else if (custom) {
				reactions.push_back({ custom });
			}
		}
		settings.setExtraFavoriteReactions(std::move(reactions));
	}
	session->local().writeSessionSettings();
	return success;
}

Json ExportAppearance() {
	const auto &s = Core::App().settings();
	const auto background = Window::Theme::Background();
	const auto &theme = background->themeObject();
	const auto &paper = background->paper();
	auto themeData = Json::object();
	if (theme.cloud.id) {
		themeData = {
			{ "kind", "cloud" },
			{ "id", std::to_string(theme.cloud.id) },
			{ "access_hash", std::to_string(theme.cloud.accessHash) },
			{ "document_id", std::to_string(theme.cloud.documentId) },
			{ "slug", ToString(theme.cloud.slug) },
		};
	} else if (Window::Theme::IsEmbeddedTheme(theme.pathAbsolute)) {
		themeData = {
			{ "kind", "embedded" },
			{ "path", ToString(theme.pathAbsolute) },
		};
	} else {
		themeData = { { "kind", "local_unsupported" } };
	}
	return Json{
		{ "language_kind", Core::App().langpack().isCustom()
			? "custom_unsupported"
			: "cloud" },
		{ "language", Core::App().langpack().isCustom()
			? std::string()
			: ToString(Core::App().langpack().id()) },
		{ "night_mode", Window::Theme::IsNightMode() },
		{ "system_dark_mode", s.systemDarkModeEnabled() },
		{ "system_accent", s.systemAccentColorEnabled() },
		{ "custom_font", ToString(s.customFontFamily()) },
		{ "accent_colors", s.themesAccentColors().serialize().toBase64().toStdString() },
		{ "theme", std::move(themeData) },
		{ "wallpaper", Data::IsCloudWallPaper(paper)
			? ToString(QString::fromLatin1(
				paper.withoutImageData().serialize().toBase64()))
			: std::string() },
		{ "wallpaper_local_unsupported", Data::IsCustomWallPaper(paper) },
	};
}

void ApplyAppearance(
		not_null<Main::Session*> session,
		const Json &data,
		std::vector<QString> &warnings) {
	auto &s = Core::App().settings();
	s.setSystemDarkModeEnabled(Read(data, "system_dark_mode", s.systemDarkModeEnabled()));
	s.setSystemAccentColorEnabled(Read(data, "system_accent", s.systemAccentColorEnabled()));
	const auto font = ReadString(data, "custom_font");
	if (font.isEmpty() || QFontDatabase::families().contains(font)) {
		s.setCustomFontFamily(font);
	} else {
		warnings.push_back(u"font:"_q + font);
	}
	const auto accents = QByteArray::fromBase64(ReadString(data, "accent_colors").toLatin1());
	if (!accents.isEmpty()) {
		auto colors = Window::Theme::AccentColors();
		if (colors.setFromSerialized(accents)) {
			s.setThemesAccentColors(std::move(colors));
		}
	}
	if (const auto i = data.find("theme"); i != data.end() && i->is_object()) {
		const auto kind = ReadString(*i, "kind");
		if (kind == u"embedded"_q) {
			const auto path = ReadString(*i, "path");
			const auto schemes = Window::Theme::EmbeddedThemes();
			if (ranges::contains(schemes, path, &Window::Theme::EmbeddedScheme::path)) {
				Window::Theme::ApplyDefaultWithPath(path);
				Window::Theme::KeepApplied();
			} else {
				warnings.push_back(u"theme:embedded"_q);
			}
		} else if (kind == u"cloud"_q) {
			const auto id = ReadString(*i, "id").toULongLong();
			const auto &themes = session->data().cloudThemes().list();
			if (const auto found = ranges::find(themes, id, &Data::CloudTheme::id);
				found != end(themes)) {
				session->data().cloudThemes().applyFromDocument(*found);
			} else {
				warnings.push_back(u"theme:cloud:"_q + QString::number(id));
			}
		} else if (kind == u"local_unsupported"_q) {
			warnings.push_back(u"theme:local"_q);
		}
	}
	Window::Theme::SetNightModeValue(Read(data, "night_mode", Window::Theme::IsNightMode()));
	const auto wallpaper = QByteArray::fromBase64(
		ReadString(data, "wallpaper").toLatin1());
	if (!wallpaper.isEmpty()) {
		if (const auto paper = Data::WallPaper::FromSerialized(wallpaper);
			paper && Data::IsCloudWallPaper(*paper)) {
			Window::Theme::Background()->set(*paper);
		} else {
			warnings.push_back(u"wallpaper:invalid"_q);
		}
	} else if (Read(data, "wallpaper_local_unsupported", false)) {
		warnings.push_back(u"wallpaper:local"_q);
	}
	const auto language = ReadString(data, "language");
	if (ReadString(data, "language_kind") == u"custom_unsupported"_q) {
		warnings.push_back(u"language:custom"_q);
	} else if (!language.isEmpty()
		&& language != Core::App().langpack().id()
		&& language != Lang::CustomLanguageId()) {
		Lang::CurrentCloudManager().switchToLanguage(language);
	}
}

base::expected<Json, QString> ParseSnapshot(const QByteArray &canonical) {
	auto result = Json();
	try {
		result = Json::parse(
			canonical.constData(),
			canonical.constData() + canonical.size());
	} catch (const std::exception &error) {
		return base::unexpected(QString::fromUtf8(error.what()));
	}
	if (!result.is_object()
		|| Read(result, "schema", 0) != 1
		|| Read(result, "min_reader_schema", 0) > 1
		|| !result.contains("categories")) {
		return base::unexpected(u"Unsupported settings snapshot."_q);
	}
	return result;
}

} // namespace

base::expected<Snapshot, QString> ExportSettingsSnapshot(
		not_null<Main::Session*> session,
		uint32_t categories,
		const QString &deviceId,
		bool includeProxies) {
	categories &= kAllCategories;
	if (!categories) {
		return base::unexpected(u"No settings categories selected."_q);
	}
	auto groups = Json::object();
	if (Has(categories, Category::AyuGlobal)) {
		groups["ayu_global"] = AyuSettings::CloudExportGlobal();
	}
	if (Has(categories, Category::TelegramGlobal)) {
		groups["telegram_global"] = ExportTelegramGlobal();
	}
	if (Has(categories, Category::AccountAndChats)) {
		groups["account_and_chats"] = ExportAccount(session);
	}
	if (Has(categories, Category::Appearance)) {
		groups["appearance"] = ExportAppearance();
	}
	auto hashes = Json::object();
	for (const auto &[key, value] : groups.items()) {
		const auto dumped = value.dump();
		hashes[key] = ToString(Sha256(QByteArray(dumped.data(), int(dumped.size()))));
	}
	auto payload = Json{
		{ "schema", 1 },
		{ "min_reader_schema", 1 },
		{ "client", "AywGram" },
		{ "client_version", AppVersionStr },
		{ "device_id", ToString(deviceId) },
		{ "account_id", std::to_string(session->userId().bare) },
		{ "categories", categories },
		{ "category_hashes", std::move(hashes) },
		{ "settings", std::move(groups) },
		{ "sync_proxies", includeProxies },
	};
	if (includeProxies) {
		payload["proxy_settings"] = ExportProxySettings();
	}
	const auto contentDumped = Json{
		{ "categories", categories },
		{ "settings", payload.at("settings") },
		{ "sync_proxies", includeProxies },
		{ "proxy_settings", includeProxies
			? payload.at("proxy_settings")
			: Json(nullptr) },
	}.dump();
	const auto dumped = payload.dump();
	const auto canonical = QByteArray(dumped.data(), int(dumped.size()));
	return Snapshot{
		.canonical = canonical,
		.hash = Sha256(canonical),
		.contentHash = Sha256(QByteArray(
			contentDumped.data(),
			int(contentDumped.size()))),
		.categories = categories,
		.accountId = session->userId().bare,
		.proxiesIncluded = includeProxies,
	};
}

base::expected<Snapshot, QString> ValidateSettingsSnapshot(
		const QByteArray &canonical,
		uint64_t expectedAccountId) {
	if (canonical.size() > kMaximumPayloadSize) {
		return base::unexpected(u"Settings snapshot is too large."_q);
	}
	const auto parsed = ParseSnapshot(canonical);
	if (!parsed) {
		return base::unexpected(parsed.error());
	}
	auto valid = false;
	const auto accountId = ReadString(*parsed, "account_id").toULongLong(&valid);
	const auto categories = Read(*parsed, "categories", uint32_t(0));
	const auto hasProxyPolicy = parsed->contains("sync_proxies");
	const auto proxiesIncluded = Read(*parsed, "sync_proxies", false);
	if (!valid
		|| accountId != expectedAccountId
		|| !categories
		|| (categories & ~kAllCategories)
		|| (hasProxyPolicy && !parsed->at("sync_proxies").is_boolean())
		|| !parsed->contains("settings")
		|| !parsed->at("settings").is_object()
		|| !parsed->contains("category_hashes")
		|| !parsed->at("category_hashes").is_object()) {
		return base::unexpected(u"Settings snapshot metadata is invalid."_q);
	}
	const auto proxySettings = parsed->find("proxy_settings");
	if (proxiesIncluded != (proxySettings != parsed->end())
		|| (proxiesIncluded && !ParseProxySettings(*proxySettings))) {
		return base::unexpected(u"Settings snapshot proxy data is invalid."_q);
	}
	const auto &settings = parsed->at("settings");
	const auto &hashes = parsed->at("category_hashes");
	for (const auto &[category, key] : {
			std::pair(Category::AyuGlobal, "ayu_global"),
			std::pair(Category::TelegramGlobal, "telegram_global"),
			std::pair(Category::AccountAndChats, "account_and_chats"),
			std::pair(Category::Appearance, "appearance") }) {
		const auto selected = Has(categories, category);
		const auto value = settings.find(key);
		const auto hash = hashes.find(key);
		if (selected != (value != settings.end())
			|| selected != (hash != hashes.end())
			|| (selected && (!value->is_object() || !hash->is_string()))) {
			return base::unexpected(u"Settings snapshot categories are invalid."_q);
		}
		if (selected) {
			const auto dumped = value->dump();
			const auto actual = ToString(Sha256(
				QByteArray(dumped.data(), int(dumped.size()))));
			if (hash->get<std::string>() != actual) {
				return base::unexpected(u"Settings snapshot category hash is invalid."_q);
			}
		}
	}
	return Snapshot{
		.canonical = canonical,
		.hash = Sha256(canonical),
		.contentHash = [&] {
			const auto content = hasProxyPolicy
				? Json{
					{ "categories", categories },
					{ "settings", settings },
					{ "sync_proxies", proxiesIncluded },
					{ "proxy_settings", proxiesIncluded
						? *proxySettings
						: Json(nullptr) },
				}
				: Json{
					{ "categories", categories },
					{ "settings", settings },
				};
			const auto dumped = content.dump();
			return Sha256(QByteArray(dumped.data(), int(dumped.size())));
		}(),
		.categories = categories,
		.accountId = accountId,
		.proxiesIncluded = proxiesIncluded,
	};
}

base::expected<std::vector<CategoryDifference>, QString>
DiffSettingsSnapshots(const Snapshot &local, const Snapshot &remote) {
	const auto localValidated = ValidateSettingsSnapshot(
		local.canonical,
		local.accountId);
	const auto remoteValidated = ValidateSettingsSnapshot(
		remote.canonical,
		remote.accountId);
	const auto localParsed = ParseSnapshot(local.canonical);
	const auto remoteParsed = ParseSnapshot(remote.canonical);
	if (!localValidated
		|| !remoteValidated
		|| !localParsed
		|| !remoteParsed
		|| local.accountId != remote.accountId) {
		return base::unexpected(u"Settings snapshots cannot be compared."_q);
	}
	const auto &localHashes = localParsed->at("category_hashes");
	const auto &remoteHashes = remoteParsed->at("category_hashes");
	auto result = std::vector<CategoryDifference>();
	for (const auto &[category, key] : {
			std::pair(Category::AyuGlobal, "ayu_global"),
			std::pair(Category::TelegramGlobal, "telegram_global"),
			std::pair(Category::AccountAndChats, "account_and_chats"),
			std::pair(Category::Appearance, "appearance") }) {
		const auto localSelected = Has(local.categories, category);
		const auto remoteSelected = Has(remote.categories, category);
		auto difference = Difference::Same;
		if (localSelected && !remoteSelected) {
			difference = Difference::LocalOnly;
		} else if (!localSelected && remoteSelected) {
			difference = Difference::RemoteOnly;
		} else if (localSelected
			&& localHashes.at(key).get<std::string>()
				!= remoteHashes.at(key).get<std::string>()) {
			difference = Difference::Different;
		}
		result.push_back({ category, difference });
	}
	return result;
}

ApplyResult ApplySettingsSnapshot(
		not_null<Main::Session*> session,
		const Snapshot &snapshot) {
	const auto parsed = ParseSnapshot(snapshot.canonical);
	if (!parsed || snapshot.accountId != session->userId().bare) {
		return {};
	}
	const auto &groups = parsed->at("settings");
	auto result = ApplyResult{ .success = true };
	auto portableProxy = std::optional<PortableProxySettings>();
	if (snapshot.proxiesIncluded) {
		const auto proxy = parsed->find("proxy_settings");
		portableProxy = (proxy == parsed->end())
			? std::optional<PortableProxySettings>()
			: ParseProxySettings(*proxy);
		if (!portableProxy) {
			return {};
		}
	}
	if (Has(snapshot.categories, Category::AyuGlobal)) {
		const auto i = groups.find("ayu_global");
		if (i == groups.end() || !AyuSettings::CloudValidateGlobal(*i)) {
			return {};
		}
	}
	if (Has(snapshot.categories, Category::AccountAndChats)) {
		const auto i = groups.find("account_and_chats");
		if (i == groups.end() || !ValidateAccount(session, *i)) {
			return {};
		}
	}
	if (Has(snapshot.categories, Category::AyuGlobal)) {
		const auto i = groups.find("ayu_global");
		if (i != groups.end() && i->is_object()) {
			const auto font = ReadString(*i, "monoFont");
			if (!font.isEmpty() && !QFontDatabase::families().contains(font)) {
				result.warnings.push_back(u"font:mono:"_q + font);
			}
		}
		result.success = (i != groups.end())
			&& AyuSettings::CloudApplyGlobal(*i)
			&& result.success;
	}
	if (Has(snapshot.categories, Category::TelegramGlobal)) {
		if (const auto i = groups.find("telegram_global");
			i != groups.end() && i->is_object()) {
			ApplyTelegramGlobal(*i);
		} else {
			result.success = false;
		}
	}
	if (Has(snapshot.categories, Category::AccountAndChats)) {
		const auto i = groups.find("account_and_chats");
		result.success = ApplyAccount(session, *i) && result.success;
	}
	if (Has(snapshot.categories, Category::Appearance)) {
		if (const auto i = groups.find("appearance");
			i != groups.end() && i->is_object()) {
			ApplyAppearance(session, *i, result.warnings);
		} else {
			result.success = false;
		}
	}
	if (portableProxy) {
		ApplyProxySettings(*portableProxy);
	}
	if (result.success) {
		Local::writeSettings();
	}
	return result;
}

} // namespace AyuCloud
