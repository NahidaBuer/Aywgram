/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

class DocumentData;
class HistoryItem;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

class ListWidget;

void AddSaveDocumentAction(
	const Ui::Menu::MenuCallback &addAction,
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document,
	not_null<Window::SessionController*> controller);

void AddSaveDocumentAction(
	not_null<Ui::PopupMenu*> menu,
	HistoryItem *item,
	not_null<DocumentData*> document,
	not_null<ListWidget*> list);

void SaveLivePhotoVideo(
	FullMsgId itemId,
	not_null<DocumentData*> document);

void AddSaveLivePhotoVideoAction(
	const Ui::Menu::MenuCallback &addAction,
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document,
	not_null<Window::SessionController*> controller);

void AddSaveLivePhotoVideoAction(
	not_null<Ui::PopupMenu*> menu,
	HistoryItem *item,
	not_null<DocumentData*> document,
	not_null<Window::SessionController*> controller);

} // namespace HistoryView
