#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] bool HasChatOverrides(not_null<PeerData*> peer);
void ShowChatOverrides(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

} // namespace Settings
