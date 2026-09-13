/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace HistoryView {

enum class DoubleClickQuickAction {
	Reply, // Default.
	React,
	None,
	Save,
	Delete,
	Repeat,
	RepeatNoQuote,
	ReactionMenu,
	Edit,
};

[[nodiscard]] DoubleClickQuickAction CurrentQuickAction(
	bool outgoing,
	bool forceClick = false);
void SetQuickAction(
	bool outgoing,
	bool forceClick,
	DoubleClickQuickAction action);
void SaveMessageQuickAction(not_null<HistoryItem*> item);

} // namespace HistoryView
