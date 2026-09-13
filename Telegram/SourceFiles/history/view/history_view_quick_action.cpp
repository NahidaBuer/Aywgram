/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_quick_action.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"
#include "apiwrap.h"

namespace HistoryView {

namespace {

std::string_view QuickActionKey(bool outgoing, bool forceClick) {
	return forceClick
		? "ayu/forceClickAction"
		: outgoing
		? "ayu/outgoingDoubleClickAction"
		: "ayu/incomingDoubleClickAction";
}

} // namespace

DoubleClickQuickAction CurrentQuickAction(bool outgoing, bool forceClick) {
	auto &settings = Core::App().settings();
	const auto fallback = forceClick
		? DoubleClickQuickAction::React
		: settings.chatQuickAction();
	const auto stored = settings.readPref<QByteArray>(
		QuickActionKey(outgoing, forceClick));
	auto valid = false;
	const auto value = stored.toInt(&valid);
	if (!valid
		|| value < int(DoubleClickQuickAction::Reply)
		|| value > int(DoubleClickQuickAction::Edit)
		|| ((!outgoing || forceClick)
			&& value == int(DoubleClickQuickAction::Edit))) {
		return fallback;
	}
	return DoubleClickQuickAction(value);
}

void SetQuickAction(
		bool outgoing,
		bool forceClick,
		DoubleClickQuickAction action) {
	Core::App().settings().writePref<QByteArray>(
		QuickActionKey(outgoing, forceClick),
		QByteArray::number(int(action)));
}

void SaveMessageQuickAction(not_null<HistoryItem*> item) {
	if (!item->isRegular()
		|| !IsServerMsgId(item->id)
		|| !item->allowsForward()) {
		return;
	}
	const auto session = &item->history()->session();
	const auto history = session->data().history(session->user());
	auto action = Api::SendAction(history);
	action.clearDraft = false;
	action.generateLocal = false;
	session->api().forwardMessages(
		history->resolveForwardDraft(Data::ForwardDraft{
			.ids = MessageIdsList{ item->fullId() },
		}),
		action,
		[](bool success) {
			if (success) {
				Ui::Toast::Show(tr::lng_share_done(tr::now));
			}
		});
}

} // namespace HistoryView
