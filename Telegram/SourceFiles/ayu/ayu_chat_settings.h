#pragma once

#include "rpl/producer.h"

class PeerData;

namespace AyuChatSettings {

enum class Feature {
	ShowScheduledButton,
	Count,
};

enum class Override {
	Default,
	Enabled,
	Disabled,
};

struct Change {
	PeerData *peer = nullptr;
	Feature feature = Feature::Count;
};

[[nodiscard]] Override GetOverride(
	not_null<PeerData*> peer,
	Feature feature);
[[nodiscard]] bool Resolve(
	not_null<PeerData*> peer,
	Feature feature);
[[nodiscard]] rpl::producer<bool> ResolvedValue(
	not_null<PeerData*> peer,
	Feature feature);
void SetOverride(
	not_null<PeerData*> peer,
	Feature feature,
	Override value);
[[nodiscard]] rpl::producer<Change> Changes();
void NotifyChange(PeerData *peer, Feature feature);

} // namespace AyuChatSettings
