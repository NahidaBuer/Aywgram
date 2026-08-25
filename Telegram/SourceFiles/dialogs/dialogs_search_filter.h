/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_messages_search.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Dialogs {

void FillSearchFilterMenu(
	not_null<Ui::PopupMenu*> menu,
	Api::SearchFilter selected,
	Fn<void(Api::SearchFilter)> callback);

} // namespace Dialogs
