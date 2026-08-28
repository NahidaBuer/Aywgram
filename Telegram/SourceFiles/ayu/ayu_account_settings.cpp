#include "ayu/ayu_account_settings.h"

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

} // namespace AyuAccountSettings
