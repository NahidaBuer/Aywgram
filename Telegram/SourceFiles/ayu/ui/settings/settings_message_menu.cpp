// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_message_menu.h"

#include "ayu/ayu_settings.h"
#include "ayu/ui/context_menu/message_menu_registry.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_chats.h"
#include "lang_auto.h"
#include "settings/settings_builder.h"
#include "styles/style_menu_icons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <algorithm>

namespace Settings {
namespace {

using namespace Builder;
using namespace AyuBuilder;

void BuildMenuItems(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	const auto visibilityOptions = std::vector{
		tr::ayu_SettingsContextMenuItemHidden(tr::now),
		tr::ayu_SettingsContextMenuItemShown(tr::now),
		tr::ayu_SettingsContextMenuItemExtended(tr::now),
	};

	ayu.addChooseButton({
		.id = u"ayu/showReactionsPanelInContextMenu"_q,
		.title = tr::ayu_SettingsContextMenuReactionsPanel(),
		.boxTitle = tr::ayu_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(
			settings->showReactionsPanelInContextMenu()),
		.options = visibilityOptions,
		.setter = [](int i) {
			AyuSettings::getInstance().setShowReactionsPanelInContextMenu(
				static_cast<ContextMenuVisibility>(i));
		},
		.icon = { &st::menuIconReactions },
	});
	ayu.addChooseButton({
		.id = u"ayu/showViewsPanelInContextMenu"_q,
		.title = tr::ayu_SettingsContextMenuViewsPanel(),
		.boxTitle = tr::ayu_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(
			settings->showViewsPanelInContextMenu()),
		.options = visibilityOptions,
		.setter = [](int i) {
			AyuSettings::getInstance().setShowViewsPanelInContextMenu(
				static_cast<ContextMenuVisibility>(i));
		},
		.icon = { &st::menuIconShowInChat },
	});

	const auto placements = std::vector{
		tr::ayu_SettingsContextMenuItemQuick(tr::now),
		tr::ayu_SettingsContextMenuItemShown(tr::now),
		tr::ayu_SettingsContextMenuItemExtended(tr::now),
		tr::ayu_SettingsContextMenuItemHidden(tr::now),
	};
	const auto submenuPlacements = std::vector{
		tr::ayu_SettingsContextMenuItemShown(tr::now),
		tr::ayu_SettingsContextMenuItemExtended(tr::now),
		tr::ayu_SettingsContextMenuItemHidden(tr::now),
	};
	for (const auto &entry : AyuUi::MessageMenu::Catalog()) {
		const auto current = settings->messageMenuPlacement(
			entry.id,
			entry.defaultPlacement);
		const auto initial = entry.submenu
			? std::clamp(int(current) - 1, 0, 2)
			: int(current);
		ayu.addChooseButton({
			.id = u"ayu/messageMenu/"_q + QString::fromStdString(entry.id),
			.title = rpl::single(entry.title),
			.boxTitle = tr::ayu_SettingsContextMenuTitle(),
			.initialSelection = initial,
			.options = entry.submenu ? submenuPlacements : placements,
			.setter = [id = entry.id, submenu = entry.submenu](int i) {
				AyuSettings::getInstance().setMessageMenuPlacement(
					id,
					static_cast<MessageMenuPlacement>(submenu ? i + 1 : i));
			},
			.icon = { entry.icon },
		});
	}

	ayu.addSettingToggle({
		.id = u"ayu/messageMenuQuickLabels"_q,
		.title = tr::ayu_SettingsContextMenuQuickLabels(),
		.getter = &AyuSettings::messageMenuQuickLabels,
		.setter = &AyuSettings::setMessageMenuQuickLabels,
		.icon = { &st::menuIconShowAll },
	});
	builder.addButton({
		.id = u"ayu/messageMenuReset"_q,
		.title = tr::ayu_SettingsContextMenuReset(),
		.icon = { &st::menuIconRestore },
		.onClick = [controller = builder.controller()] {
			AyuSettings::getInstance().resetMessageMenuPlacements();
			controller->showToast(tr::lng_box_done(tr::now));
		},
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_SettingsContextMenuDescription());
	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuMessageMenu::Id(),
	.parentId = AyuChats::Id(),
	.title = &tr::ayu_SettingsMessageMenuLayout,
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);
	builder.addSkip();
	BuildMenuItems(builder, ayu);
});

} // namespace

AyuMessageMenu::AyuMessageMenu(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> AyuMessageMenu::title() {
	return tr::ayu_SettingsMessageMenuLayout();
}

void AyuMessageMenu::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
