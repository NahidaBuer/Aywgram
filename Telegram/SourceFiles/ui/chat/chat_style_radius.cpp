/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_style_radius.h"
#include "ui/chat/chat_style.h"
#include "base/options.h"

#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_chat.h"

#include "ayu/ayu_settings.h"

namespace Ui {
namespace {

constexpr auto kBubbleRadiusSliderMax = 16;
auto BubbleRadiusOverride = -1;

[[nodiscard]] int MapBubbleRadius(int sliderValue, int maximum) {
	const auto result = (sliderValue * maximum + (kBubbleRadiusSliderMax / 2))
		/ kBubbleRadiusSliderMax;
	return (result < 0) ? 0 : (result > maximum) ? maximum : result;
}

[[nodiscard]] int EffectiveBubbleRadiusValue() {
	return (BubbleRadiusOverride >= 0)
		? BubbleRadiusOverride
		: AyuSettings::getInstance().appliedMessageBubbleRadius();
}

[[nodiscard]] int BubbleRadiusFor(int maximum) {
	const auto value = EffectiveBubbleRadiusValue();
	if (value <= 0) {
		return 0;
	} else if (value >= kBubbleRadiusSliderMax) {
		return maximum;
	}
	return MapBubbleRadius(value, maximum);
}

base::options::toggle UseSmallMsgBubbleRadius({
	.id = kOptionUseSmallMsgBubbleRadius,
	.name = "Use small message bubble radius",
	.description = "Makes most message bubbles square-ish.",
	.restartRequired = true,
});

} // namespace

const char kOptionUseSmallMsgBubbleRadius[] = "use-small-msg-bubble-radius";

void SetBubbleRadiusOverride(int value) {
	BubbleRadiusOverride = value;
}

void ClearBubbleRadiusOverride() {
	BubbleRadiusOverride = -1;
}

int BubbleRadiusSmall() {
	return BubbleRadiusFor(st::bubbleRadiusSmall);
}

int BubbleRadiusLarge() {
	return UseSmallMsgBubbleRadius.value()
		? BubbleRadiusSmall()
		: BubbleRadiusFor(st::bubbleRadiusLarge);
}

int MsgFileThumbRadiusSmall() {
	return st::msgFileThumbRadiusSmall;
}

int MsgFileThumbRadiusLarge() {
	return UseSmallMsgBubbleRadius.value()
		? st::msgFileThumbRadiusSmall
		: st::msgFileThumbRadiusLarge;
}

}
