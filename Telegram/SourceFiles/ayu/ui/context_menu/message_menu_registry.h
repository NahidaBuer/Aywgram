#pragma once

#include "ayu/ayu_settings.h"
#include "ui/style/style_core_types.h"

#include <string>
#include <vector>

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace AyuUi::MessageMenu {

struct CatalogEntry {
	std::string id;
	QString title;
	const style::icon *icon = nullptr;
	MessageMenuPlacement defaultPlacement = MessageMenuPlacement::Normal;
	bool submenu = false;
};

[[nodiscard]] std::vector<CatalogEntry> Catalog();
[[nodiscard]] bool ShouldConstruct(
	const std::string &id,
	MessageMenuPlacement fallback,
	bool submenu = false);
void ApplyLayout(not_null<Ui::PopupMenu*> menu);

} // namespace AyuUi::MessageMenu
