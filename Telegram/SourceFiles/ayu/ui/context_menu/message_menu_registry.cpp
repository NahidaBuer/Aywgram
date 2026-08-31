#include "ayu/ui/context_menu/message_menu_registry.h"

#include "base/qt/qt_key_modifiers.h"
#include "lang_auto.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_menu_icons.h"
#include "ui/painter.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_separator.h"
#include "ui/widgets/popup_menu.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtCore/QPointer>

namespace AyuUi::MessageMenu {
namespace {

struct MatchEntry {
	CatalogEntry catalog;
	std::vector<QString> labels;
};

std::vector<MatchEntry> Entries() {
	using Placement = MessageMenuPlacement;
	return {
		{{ "reply", tr::lng_context_reply_msg(tr::now), &st::menuIconReply },
			{ tr::lng_context_reply_msg(tr::now), tr::lng_context_quote_and_reply(tr::now), tr::lng_context_reply_to_task(tr::now) }},
		{{ "edit", tr::lng_context_edit_msg(tr::now), &st::menuIconEdit },
			{ tr::lng_context_edit_msg(tr::now) }},
		{{ "copy_text", tr::lng_context_copy_text(tr::now), &st::menuIconCopy },
			{ tr::lng_context_copy_text(tr::now) }},
		{{ "copy_selection", tr::lng_context_copy_selected(tr::now), &st::menuIconCopy },
			{ tr::lng_context_copy_selected(tr::now), tr::lng_context_copy_selected_items(tr::now) }},
		{{ "translate", tr::lng_context_translate(tr::now), &st::menuIconTranslate },
			{ tr::lng_context_translate(tr::now), tr::lng_context_translate_selected(tr::now) }},
		{{ "forward", tr::lng_context_forward_msg(tr::now), &st::menuIconForward },
			{ tr::lng_context_forward_msg(tr::now), tr::lng_context_forward_selected(tr::now) }},
		{{ "forward_no_quote", tr::ayu_ContextForwardMsgNoQuote(tr::now), &st::menuIconUserHide },
			{ tr::ayu_ContextForwardMsgNoQuote(tr::now), tr::ayu_ContextForwardSelectedNoQuote(tr::now) }},
		{{ "forward_no_caption", tr::ayu_ContextForwardMsgNoCaption(tr::now), &st::menuIconCaptionHide },
			{ tr::ayu_ContextForwardMsgNoCaption(tr::now), tr::ayu_ContextForwardSelectedNoCaption(tr::now) }},
		{{ "forward_saved", tr::ayu_ForwardToSavedMessage(tr::now), &st::menuIconFave },
			{ tr::ayu_ForwardToSavedMessage(tr::now) }},
		{{ "pin", tr::lng_context_pin_msg(tr::now), &st::menuIconPin },
			{ tr::lng_context_pin_msg(tr::now), tr::lng_context_unpin_msg(tr::now), tr::lng_context_unpin_selected(tr::now) }},
		{{ "delete", tr::lng_context_delete_msg(tr::now), &st::menuIconDelete },
			{ tr::lng_context_delete_msg(tr::now), tr::lng_context_delete_selected(tr::now) }},
		{{ "report", tr::lng_context_report_msg(tr::now), &st::menuIconReport },
			{ tr::lng_context_report_msg(tr::now) }},
		{{ "select", tr::lng_context_select_msg(tr::now), &st::menuIconSelect },
			{ tr::lng_context_select_msg(tr::now), tr::lng_context_select_msg_bulk(tr::now) }},
		{{ "send_now", tr::lng_context_send_now_msg(tr::now), &st::menuIconSend },
			{ tr::lng_context_send_now_msg(tr::now), tr::lng_context_send_now_selected(tr::now) }},
		{{ "reschedule", tr::lng_context_reschedule(tr::now), &st::menuIconReschedule },
			{ tr::lng_context_reschedule(tr::now), tr::lng_context_reschedule_selected(tr::now) }},
		{{ "save_image", tr::lng_context_save_image(tr::now), &st::menuIconSaveImage },
			{ tr::lng_context_save_image(tr::now) }},
		{{ "copy_image", tr::lng_context_copy_image(tr::now), &st::menuIconCopy },
			{ tr::lng_context_copy_image(tr::now) }},
		{{ "copy_filename", tr::lng_context_copy_filename(tr::now), &st::menuIconCopy },
			{ tr::lng_context_copy_filename(tr::now) }},
		{{ "copy_phone", tr::lng_profile_copy_phone(tr::now), &st::menuIconCopy },
			{ tr::lng_profile_copy_phone(tr::now) }},
		{{ "copy_message_link", tr::lng_context_copy_message_link(tr::now), &st::menuIconLink },
			{ tr::lng_context_copy_message_link(tr::now), tr::lng_context_copy_post_link(tr::now) }},
		{{ "copy_poll_option", tr::lng_context_copy_poll_option(tr::now), &st::menuIconCopy },
			{ tr::lng_context_copy_poll_option(tr::now) }},
		{{ "copy_poll_option_link", tr::lng_context_copy_poll_option_link(tr::now), &st::menuIconLink },
			{ tr::lng_context_copy_poll_option_link(tr::now) }},
		{{ "delete_poll_option", tr::lng_context_delete_poll_option(tr::now), &st::menuIconDelete },
			{ tr::lng_context_delete_poll_option(tr::now) }},
		{{ "cancel_download", tr::lng_context_cancel_download(tr::now), &st::menuIconCancel },
			{ tr::lng_context_cancel_download(tr::now), tr::lng_context_cancel_upload(tr::now) }},
		{{ "open_gif", tr::lng_context_open_gif(tr::now), &st::menuIconShowInChat },
			{ tr::lng_context_open_gif(tr::now) }},
		{{ "save_gif", tr::lng_context_save_gif(tr::now), &st::menuIconGif },
			{ tr::lng_context_save_gif(tr::now) }},
		{{ "show_in_folder", tr::lng_context_show_in_folder(tr::now), &st::menuIconShowInChat },
			{ tr::lng_context_show_in_folder(tr::now), tr::lng_context_show_in_finder(tr::now) }},
		{{ "edit_upload_caption", tr::lng_context_upload_edit_caption(tr::now), &st::menuIconEdit },
			{ tr::lng_context_upload_edit_caption(tr::now) }},
		{{ "clear_selection", tr::lng_context_clear_selection(tr::now), &st::menuIconSelect },
			{ tr::lng_context_clear_selection(tr::now) }},
		{{ "go_to_message", tr::lng_context_to_msg(tr::now), &st::menuIconShowInChat },
			{ tr::lng_context_to_msg(tr::now), tr::ayu_ContextToSharedMedia(tr::now) }},
		{{ "view_thread", tr::lng_replies_view_thread(tr::now), &st::menuIconShowInChat },
			{ tr::lng_replies_view_thread(tr::now), tr::lng_replies_view_topic(tr::now) }},
		{{ "attached_stickers", tr::lng_context_attached_stickers(tr::now), &st::menuIconInfo,
			Placement::Normal, true }, { tr::lng_context_attached_stickers(tr::now) }},
		{{ "add_sticker_pack", tr::lng_context_pack_add(tr::now), &st::menuIconAddToFolder },
			{ tr::lng_context_pack_add(tr::now) }},
		{{ "sticker_pack_info", tr::lng_context_pack_info(tr::now), &st::menuIconInfo },
			{ tr::lng_context_pack_info(tr::now) }},
		{{ "favorite_sticker", tr::lng_faved_stickers_add(tr::now), &st::menuIconFave },
			{ tr::lng_faved_stickers_add(tr::now), tr::lng_faved_stickers_remove(tr::now) }},
		{{ "save_custom_sound", tr::lng_context_save_custom_sound(tr::now), &st::menuIconSaveImage },
			{ tr::lng_context_save_custom_sound(tr::now) }},
		{{ "add_factcheck", tr::lng_context_add_factcheck(tr::now), &st::menuIconEdit },
			{ tr::lng_context_add_factcheck(tr::now), tr::lng_context_edit_factcheck(tr::now) }},
		{{ "add_offer", tr::lng_context_add_offer(tr::now), &st::menuIconTagSell },
			{ tr::lng_context_add_offer(tr::now) }},
		{{ "send_gift", tr::lng_context_gift_send(tr::now), &st::menuIconGiftPremium },
			{ tr::lng_context_gift_send(tr::now) }},
		{{ "filter_by_tag", tr::lng_context_filter_by_tag(tr::now), &st::menuIconAddToFolder },
			{ tr::lng_context_filter_by_tag(tr::now), tr::lng_context_remove_tag(tr::now) }},
		{{ "rename_tag", tr::lng_context_tag_add_name(tr::now), &st::menuIconTagRename },
			{ tr::lng_context_tag_add_name(tr::now), tr::lng_context_tag_edit_name(tr::now) }},
		{{ "poll_retract", tr::lng_polls_retract(tr::now), &st::menuIconRestore },
			{ tr::lng_polls_retract(tr::now) }},
		{{ "poll_stop", tr::lng_polls_stop(tr::now), &st::menuIconCancel },
			{ tr::lng_polls_stop(tr::now) }},
		{{ "poll_stats", tr::lng_polls_view_stats(tr::now), &st::menuIconStats },
			{ tr::lng_polls_view_stats(tr::now), tr::lng_stats_title(tr::now) }},
		{{ "block_sender", tr::lng_profile_block_user(tr::now), &st::menuIconBlock },
			{ tr::lng_profile_block_user(tr::now) }},
		{{ "revert_ephemeral", tr::lng_ephemeral_revert(tr::now), &st::menuIconRestore },
			{ tr::lng_ephemeral_revert(tr::now) }},
		{{ "ayu_menu", u"AywGram"_q, &st::menuIconGroupReactions,
			Placement::Normal, true }, { u"AywGram"_q }},
		{{ "edits_history", tr::ayu_EditsHistoryMenuText(tr::now), &st::ayuEditsHistoryIcon },
			{ tr::ayu_EditsHistoryMenuText(tr::now) }},
		{{ "read_until", tr::ayu_ReadUntilMenuText(tr::now), &st::menuIconShowInChat },
			{ tr::ayu_ReadUntilMenuText(tr::now) }},
		{{ "expire_media", tr::ayu_ExpireMediaContextMenuText(tr::now), &st::menuIconTTLAny },
			{ tr::ayu_ExpireMediaContextMenuText(tr::now) }},
		{{ "hide_message", tr::ayu_ContextHideMessage(tr::now), &st::menuIconClear,
			Placement::Hidden }, { tr::ayu_ContextHideMessage(tr::now) }},
		{{ "user_messages", tr::ayu_UserMessagesMenuText(tr::now), &st::menuIconTTL,
			Placement::Extended }, { tr::ayu_UserMessagesMenuText(tr::now) }},
		{{ "repeat", tr::ayu_RepeatMessage(tr::now), &st::ayuRepeatMenuIcon,
			Placement::Hidden }, { tr::ayu_RepeatMessage(tr::now) }},
		{{ "details", tr::ayu_MessageDetailsPC(tr::now), &st::menuIconInfo,
			Placement::Extended, true }, { tr::ayu_MessageDetailsPC(tr::now) }},
		{{ "add_filter", tr::ayu_RegexFilterQuickAdd(tr::now), &st::menuIconAddToFolder },
			{ tr::ayu_RegexFilterQuickAdd(tr::now) }},
	};
}

const MatchEntry *FindEntry(
		const QString &text,
		const std::vector<MatchEntry> &entries) {
	for (const auto &entry : entries) {
		if (ranges::contains(entry.labels, text)) {
			return &entry;
		}
	}
	return nullptr;
}

struct QuickEntry {
	not_null<QAction*> action;
	QString title;
	const style::icon *icon = nullptr;
};

class QuickGrid final : public Ui::Menu::ItemBase {
public:
	QuickGrid(
		not_null<Ui::PopupMenu*> popup,
		std::vector<QuickEntry> entries,
		bool labels);

	QAccessible::Role accessibilityRole() override {
		return QAccessible::Role::ToolBar;
	}
	QString accessibilityName() override;
	bool isEnabled() const override { return true; }
	not_null<QAction*> action() const override { return _action; }
	void handleKeyPress(not_null<QKeyEvent*> event) override;

protected:
	int contentHeight() const override;
	void paintEvent(QPaintEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void leaveEventHook(QEvent *event) override;

private:
	int indexAt(QPoint position) const;
	void trigger(int index);

	const not_null<Ui::PopupMenu*> _popup;
	const style::Menu &_st;
	const std::vector<QuickEntry> _entries;
	const not_null<QAction*> _action;
	const bool _labels;
	const int _columns;
	int _hovered = -1;
	int _pressed = -1;
};

QuickGrid::QuickGrid(
		not_null<Ui::PopupMenu*> popup,
		std::vector<QuickEntry> entries,
		bool labels)
: ItemBase(popup->menu(), popup->st().menu)
, _popup(popup)
, _st(popup->st().menu)
, _entries(std::move(entries))
, _action(Ui::CreateChild<QAction>(popup->menu().get()))
, _labels(labels)
, _columns(std::min(4, int(_entries.size()))) {
	setMouseTracking(true);
	setAcceptBoth(true);
	enableMouseSelecting();
	fitToMenuWidth();
	setMinWidth(std::max(1, _columns) * (_labels ? 84 : 56));
	setToolTip(accessibilityName());
}

QString QuickGrid::accessibilityName() {
	auto result = QStringList();
	for (const auto &entry : _entries) {
		result.push_back(entry.title);
	}
	return result.join(u", "_q);
}

int QuickGrid::contentHeight() const {
	const auto rows = (int(_entries.size()) + _columns - 1) / _columns;
	return rows * (_labels ? 64 : 48) + 8;
}

int QuickGrid::indexAt(QPoint position) const {
	if (_entries.empty() || !rect().contains(position)) {
		return -1;
	}
	const auto cellWidth = width() / _columns;
	const auto cellHeight = _labels ? 64 : 48;
	const auto column = std::min(_columns - 1, position.x() / cellWidth);
	const auto row = position.y() / cellHeight;
	const auto index = row * _columns + column;
	return (index >= 0 && index < int(_entries.size())) ? index : -1;
}

void QuickGrid::paintEvent(QPaintEvent *event) {
	Painter p(this);
	p.fillRect(event->rect(), _st.itemBg);
	const auto cellWidth = width() / _columns;
	const auto cellHeight = _labels ? 64 : 48;
	for (auto i = 0; i != int(_entries.size()); ++i) {
		const auto cell = QRect(
			(i % _columns) * cellWidth,
			(i / _columns) * cellHeight,
			cellWidth,
			cellHeight);
		if (i == _hovered) {
			p.fillRect(cell.marginsRemoved(QMargins(2, 2, 2, 2)), _st.itemBgOver);
		}
		if (const auto icon = _entries[i].icon) {
			const auto left = cell.x() + (cell.width() - icon->width()) / 2;
			const auto top = cell.y() + (_labels ? 8 : (cell.height() - icon->height()) / 2);
			icon->paint(p, left, top, width());
		}
		if (_labels) {
			p.setFont(_st.itemStyle.font);
			p.setPen((i == _hovered) ? _st.itemFgOver : _st.itemFg);
			p.drawText(
				cell.adjusted(4, 34, -4, -4),
				Qt::AlignHCenter | Qt::AlignTop,
				_st.itemStyle.font->elided(
					_entries[i].title,
					cell.width() - 8));
		}
	}
}

void QuickGrid::mouseMoveEvent(QMouseEvent *event) {
	const auto index = indexAt(event->position().toPoint());
	if (_hovered != index) {
		_hovered = index;
		update();
	}
}

void QuickGrid::mousePressEvent(QMouseEvent *event) {
	_pressed = indexAt(event->position().toPoint());
}

void QuickGrid::mouseReleaseEvent(QMouseEvent *event) {
	const auto index = indexAt(event->position().toPoint());
	if (index == _pressed) {
		trigger(index);
	}
	_pressed = -1;
}

void QuickGrid::leaveEventHook(QEvent *event) {
	ItemBase::leaveEventHook(event);
	_hovered = -1;
	_pressed = -1;
	update();
}

void QuickGrid::handleKeyPress(not_null<QKeyEvent*> event) {
	if (_entries.empty()) {
		return;
	}
	const auto current = (_hovered < 0) ? 0 : _hovered;
	switch (event->key()) {
	case Qt::Key_Left: _hovered = std::max(0, current - 1); break;
	case Qt::Key_Right: _hovered = std::min(int(_entries.size()) - 1, current + 1); break;
	case Qt::Key_Up: _hovered = std::max(0, current - _columns); break;
	case Qt::Key_Down: _hovered = std::min(int(_entries.size()) - 1, current + _columns); break;
	case Qt::Key_Return:
	case Qt::Key_Enter:
	case Qt::Key_Space: trigger(current); return;
	default: return;
	}
	update();
}

void QuickGrid::trigger(int index) {
	if (index < 0 || index >= int(_entries.size())) {
		return;
	}
	const auto action = QPointer<QAction>(_entries[index].action.get());
	_popup->hideMenu();
	if (action) {
		action->trigger();
	}
}

void RemoveAction(not_null<Ui::PopupMenu*> menu, int index, bool keep) {
	const auto action = menu->actions()[index];
	if (keep) {
		action->setParent(menu.get());
	}
	menu->removeAction(index);
}

void NormalizeSeparators(not_null<Ui::PopupMenu*> menu) {
	auto previousSeparator = true;
	for (auto i = 0; i < menu->actions().size();) {
		const auto separator = menu->actions()[i]->isSeparator();
		if (separator && previousSeparator) {
			RemoveAction(menu, i, false);
			continue;
		}
		previousSeparator = separator;
		++i;
	}
	if (!menu->actions().empty() && menu->actions().back()->isSeparator()) {
		RemoveAction(menu, int(menu->actions().size()) - 1, false);
	}
}

} // namespace

std::vector<CatalogEntry> Catalog() {
	auto result = std::vector<CatalogEntry>();
	for (const auto &entry : Entries()) {
		result.push_back(entry.catalog);
	}
	return result;
}

bool ShouldConstruct(
		const std::string &id,
		MessageMenuPlacement fallback,
		bool submenu) {
	auto placement = AyuSettings::getInstance().messageMenuPlacement(
		id,
		fallback);
	if (submenu && placement == MessageMenuPlacement::Quick) {
		placement = MessageMenuPlacement::Normal;
	}
	return placement != MessageMenuPlacement::Hidden
		&& (placement != MessageMenuPlacement::Extended
			|| base::IsShiftPressed());
}

void ApplyLayout(not_null<Ui::PopupMenu*> menu) {
	const auto &settings = AyuSettings::getInstance();
	const auto extended = base::IsShiftPressed();
	const auto entries = Entries();
	auto quick = std::vector<QuickEntry>();
	for (auto i = 0; i < menu->actions().size();) {
		const auto action = menu->actions()[i];
		if (action->isSeparator()) {
			++i;
			continue;
		}
		const auto entry = FindEntry(action->text(), entries);
		if (!entry) {
			++i;
			continue;
		}
		auto placement = settings.messageMenuPlacement(
			entry->catalog.id,
			entry->catalog.defaultPlacement);
		if (entry->catalog.submenu && placement == MessageMenuPlacement::Quick) {
			placement = MessageMenuPlacement::Normal;
		}
		if (placement == MessageMenuPlacement::Quick
			&& !dynamic_cast<Ui::Menu::Action*>(
				menu->menu()->itemForAction(action))) {
			placement = MessageMenuPlacement::Normal;
		}
		if (placement == MessageMenuPlacement::Hidden
			|| (placement == MessageMenuPlacement::Extended && !extended)) {
			RemoveAction(menu, i, false);
			continue;
		} else if (placement == MessageMenuPlacement::Quick) {
			quick.push_back({
				.action = action,
				.title = entry->catalog.title,
				.icon = entry->catalog.icon,
			});
			RemoveAction(menu, i, true);
			continue;
		}
		++i;
	}
	NormalizeSeparators(menu);
	if (!quick.empty()) {
		const auto hadNormalActions = !menu->actions().empty();
		menu->insertAction(0, base::make_unique_q<QuickGrid>(
			menu,
			std::move(quick),
			settings.messageMenuQuickLabels()));
		if (hadNormalActions) {
			const auto action = new QAction(menu->menu());
			action->setSeparator(true);
			menu->insertAction(1, base::make_unique_q<Ui::Menu::Separator>(
				menu->menu(),
				menu->st().menu,
				menu->st().menu.separator,
				action));
		}
	}
}

} // namespace AyuUi::MessageMenu
