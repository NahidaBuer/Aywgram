#include "ayu/ui/settings/settings_chat_overrides.h"

#include "ayu/ayu_chat_settings.h"
#include "data/data_peer.h"
#include "lang_auto.h"
#include "settings/settings_common.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <memory>
#include <rpl/variable.h>

#include "styles/style_settings.h"

namespace Settings {
namespace {

using Feature = AyuChatSettings::Feature;
using Override = AyuChatSettings::Override;

bool ShowScheduledButtonAvailable(not_null<PeerData*> peer) {
	return !peer->isRepliesChat();
}

int OverrideIndex(Override value) {
	switch (value) {
	case Override::Default:
		return 0;
	case Override::Enabled:
		return 1;
	case Override::Disabled:
		return 2;
	}
	Unexpected("Unknown Ayu chat override.");
}

Override OverrideFromIndex(int index) {
	switch (index) {
	case 0:
		return Override::Default;
	case 1:
		return Override::Enabled;
	case 2:
		return Override::Disabled;
	}
	Unexpected("Unknown Ayu chat override index.");
}

rpl::producer<QString> OverrideLabel(rpl::producer<Override> value) {
	return rpl::combine(
		std::move(value),
		tr::ayu_ChatOverrideDefault(),
		tr::ayu_ChatOverrideEnabled(),
		tr::ayu_ChatOverrideDisabled()
	) | rpl::map([](
			Override value,
			QString defaultLabel,
			QString enabledLabel,
			QString disabledLabel) {
		switch (value) {
		case Override::Default:
			return defaultLabel;
		case Override::Enabled:
			return enabledLabel;
		case Override::Disabled:
			return disabledLabel;
		}
		Unexpected("Unknown Ayu chat override.");
	});
}

} // namespace

bool HasChatOverrides(not_null<PeerData*> peer) {
	return ShowScheduledButtonAvailable(peer);
}

void ShowChatOverrides(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::ayu_ChatOverrides());
		const auto content = box->verticalLayout();
		Ui::AddDividerText(content, tr::ayu_ChatOverridesDescription());
		Ui::AddSkip(content);

		const auto value = std::make_shared<rpl::variable<Override>>(
			AyuChatSettings::GetOverride(
				peer,
				Feature::ShowScheduledButton));
		const auto button = AddButtonWithLabel(
			content,
			tr::ayu_AlwaysShowScheduledButton(),
			OverrideLabel(value->value()),
			st::settingsButtonNoIcon);
		button->addClickHandler([=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> choice) {
				SingleChoiceBox(choice, {
					.title = tr::ayu_AlwaysShowScheduledButton(),
					.options = {
						tr::ayu_ChatOverrideDefault(tr::now),
						tr::ayu_ChatOverrideEnabled(tr::now),
						tr::ayu_ChatOverrideDisabled(tr::now),
					},
					.initialSelection = OverrideIndex(value->current()),
					.callback = [=](int index) {
						const auto selected = OverrideFromIndex(index);
						AyuChatSettings::SetOverride(
							peer,
							Feature::ShowScheduledButton,
							selected);
						*value = selected;
					},
				});
			}));
		});
		Ui::AddSkip(content);
	}));
}

} // namespace Settings
