/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_search_filter.h"

#include "ui/widgets/popup_menu.h"

#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"

namespace Dialogs {
namespace {

[[nodiscard]] const style::icon &SearchFilterIcon(Api::SearchFilter filter) {
	switch (filter) {
	case Api::SearchFilter::NoFilter:
		return st::menuIconShowAll;
	case Api::SearchFilter::Photos:
		return st::menuIconPhoto;
	case Api::SearchFilter::Videos:
		return st::dialogsSearchInMessageTypeVideo;
	case Api::SearchFilter::Files:
		return st::menuIconFile;
	case Api::SearchFilter::Links:
		return st::menuIconLink;
	case Api::SearchFilter::Music:
		return st::dialogsSearchInMessageTypeMusic;
	case Api::SearchFilter::VoiceMessages:
		return st::dialogsSearchInMessageTypeVoice;
	case Api::SearchFilter::VideoMessages:
		return st::dialogsSearchInMessageTypeRoundVideo;
	case Api::SearchFilter::Gifs:
		return st::menuIconGif;
	case Api::SearchFilter::Polls:
		return st::menuIconCreatePoll;
	case Api::SearchFilter::MyMentions:
		return st::dialogsSearchInMessageTypeMention;
	case Api::SearchFilter::Locations:
		return st::menuIconAddress;
	case Api::SearchFilter::Pinned:
		return st::menuIconPin;
	}
	Unexpected("SearchFilter in SearchFilterIcon.");
}

} // namespace

void FillSearchFilterMenu(
		not_null<Ui::PopupMenu*> menu,
		Api::SearchFilter selected,
		Fn<void(Api::SearchFilter)> callback) {
	for (const auto filter : Api::SearchFilters()) {
		const auto icon = (filter == selected)
			? &st::dialogsSearchInCheck
			: &SearchFilterIcon(filter);
		menu->addAction(
			Api::SearchFilterLabel(filter),
			[=] { callback(filter); },
			icon,
			icon);
	}
}

} // namespace Dialogs
