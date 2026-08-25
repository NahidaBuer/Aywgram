/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_search_filter.h"

#include "ui/widgets/popup_menu.h"

#include "styles/style_dialogs.h"

namespace Dialogs {

void FillSearchFilterMenu(
		not_null<Ui::PopupMenu*> menu,
		Api::SearchFilter selected,
		Fn<void(Api::SearchFilter)> callback) {
	for (const auto filter : Api::SearchFilters()) {
		const auto chosen = (filter == selected);
		menu->addAction(
			Api::SearchFilterLabel(filter),
			[=] { callback(filter); },
			chosen ? &st::dialogsSearchInCheck : nullptr,
			chosen ? &st::dialogsSearchInCheck : nullptr);
	}
}

} // namespace Dialogs
