#include "ayu/ayu_account_settings.h"
#include "ayu/cloud/ayu_settings_sync.h"

#include "apiwrap.h"
#include "core/application.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <string_view>

namespace AyuAccountSettings {
namespace {

constexpr auto kIgnoreRemoteText = "ayu.drafts.ignore_remote_text";
constexpr auto kBlockLocalTextUpload = "ayu.drafts.block_local_text_upload";

void SnapshotDrafts(not_null<Main::Session*> session) {
	Core::App().materializeLocalDrafts();
	session->data().histories().enumerate([&](not_null<History*> history) {
		session->local().writeDrafts(history);
		session->local().writeDraftCursors(history);
	});
}

void Set(not_null<Main::Session*> session, std::string_view key, bool value) {
	if (session->local().readPref<bool>(key) == value) {
		return;
	}
	session->local().writePref<bool>(key, value);
	if (value) {
		SnapshotDrafts(session);
	}
	session->api().cloudDraftIsolationChanged();
	AyuCloud::MarkSettingsDirty();
}

} // namespace

bool IgnoreRemoteText(not_null<Main::Session*> session) {
	return session->local().readPref<bool>(kIgnoreRemoteText);
}

bool BlockLocalTextUpload(not_null<Main::Session*> session) {
	return session->local().readPref<bool>(kBlockLocalTextUpload);
}

bool IsolationEnabled(not_null<Main::Session*> session) {
	return IgnoreRemoteText(session) || BlockLocalTextUpload(session);
}

void SetIgnoreRemoteText(not_null<Main::Session*> session, bool value) {
	Set(session, kIgnoreRemoteText, value);
}

void SetBlockLocalTextUpload(not_null<Main::Session*> session, bool value) {
	Set(session, kBlockLocalTextUpload, value);
}

nlohmann::json CloudExport(not_null<Main::Session*> session) {
	return nlohmann::json{
		{ "ignore_remote_text", IgnoreRemoteText(session) },
		{ "block_local_text_upload", BlockLocalTextUpload(session) },
	};
}

bool CloudValidate(const nlohmann::json &data) {
	if (!data.is_object()) {
		return false;
	}
	for (const auto key : {
			"ignore_remote_text",
			"block_local_text_upload" }) {
		const auto value = data.find(key);
		if (value == data.end() || !value->is_boolean()) {
			return false;
		}
	}
	return true;
}

bool CloudApply(
		not_null<Main::Session*> session,
		const nlohmann::json &data) {
	if (!CloudValidate(data)) {
		return false;
	}
	if (const auto value = data.find("ignore_remote_text");
		value != data.end() && value->is_boolean()) {
		SetIgnoreRemoteText(session, value->get<bool>());
	}
	if (const auto value = data.find("block_local_text_upload");
		value != data.end() && value->is_boolean()) {
		SetBlockLocalTextUpload(session, value->get<bool>());
	}
	return true;
}

} // namespace AyuAccountSettings
