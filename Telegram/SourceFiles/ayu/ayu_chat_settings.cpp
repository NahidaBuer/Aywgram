#include "ayu/ayu_chat_settings.h"

#include "ayu/ayu_settings.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "main/main_session.h"
#include "rpl/event_stream.h"
#include "storage/storage_account.h"

#include <array>
#include <string_view>

namespace AyuChatSettings {
namespace {

using GlobalValue = bool (*)();

struct Descriptor {
	Feature feature = Feature::Count;
	std::string_view storageKey;
	GlobalValue globalValue = nullptr;
};

rpl::event_stream<Change> ChangeEvents;

bool ShowScheduledButtonGlobalValue() {
	return AyuSettings::getInstance().alwaysShowScheduledButton();
}

constexpr auto kDescriptors = std::array{
	Descriptor{
		.feature = Feature::ShowScheduledButton,
		.storageKey = "show_scheduled_button",
		.globalValue = ShowScheduledButtonGlobalValue,
	},
};
static_assert(kDescriptors.size() == static_cast<std::size_t>(Feature::Count));

const Descriptor &DescriptorFor(Feature feature) {
	for (const auto &descriptor : kDescriptors) {
		if (descriptor.feature == feature) {
			return descriptor;
		}
	}
	Unexpected("Unknown Ayu chat feature.");
}

QByteArray StorageKeyFor(PeerId peerId, const Descriptor &descriptor) {
	auto result = QByteArray("ayu.chat_override.");
	result.append(QByteArray::number(SerializePeerId(peerId)));
	result.append('.');
	result.append(
		descriptor.storageKey.data(),
		static_cast<int>(descriptor.storageKey.size()));
	return result;
}

std::string_view PrefKey(const QByteArray &key) {
	return { key.constData(), static_cast<std::size_t>(key.size()) };
}

Override ReadOverride(
		not_null<PeerData*> peer,
		const Descriptor &descriptor) {
	const auto canonical = peer->migrateToOrMe();
	auto &local = peer->session().local();
	const auto key = StorageKeyFor(canonical->id, descriptor);
	auto stored = local.readPrefOptional<bool>(PrefKey(key));
	if (!stored) {
		if (const auto migrated = canonical->migrateFrom()) {
			const auto migratedKey = StorageKeyFor(migrated->id, descriptor);
			stored = local.readPrefOptional<bool>(PrefKey(migratedKey));
		}
	}
	if (!stored) {
		return Override::Default;
	}
	return *stored ? Override::Enabled : Override::Disabled;
}

} // namespace

Override GetOverride(not_null<PeerData*> peer, Feature feature) {
	return ReadOverride(peer, DescriptorFor(feature));
}

bool Resolve(not_null<PeerData*> peer, Feature feature) {
	const auto &descriptor = DescriptorFor(feature);
	Expects(descriptor.globalValue != nullptr);
	switch (ReadOverride(peer, descriptor)) {
	case Override::Default:
		return descriptor.globalValue();
	case Override::Enabled:
		return true;
	case Override::Disabled:
		return false;
	}
	Unexpected("Unknown Ayu chat feature override.");
}

rpl::producer<bool> ResolvedValue(
		not_null<PeerData*> peer,
		Feature feature) {
	const auto canonical = peer->migrateToOrMe();
	return rpl::single(Resolve(peer, feature)) | rpl::then(
		Changes(
		) | rpl::filter([=](const Change &change) {
			return (change.feature == feature)
				&& (!change.peer
					|| (change.peer->migrateToOrMe() == canonical));
		}) | rpl::map([=](const Change &) {
			return Resolve(peer, feature);
		}));
}

void SetOverride(
		not_null<PeerData*> peer,
		Feature feature,
		Override value) {
	const auto &descriptor = DescriptorFor(feature);
	const auto canonical = peer->migrateToOrMe();
	const auto key = StorageKeyFor(canonical->id, descriptor);
	auto &local = peer->session().local();
	switch (value) {
	case Override::Default:
		local.clearPref(PrefKey(key));
		if (const auto migrated = canonical->migrateFrom()) {
			const auto migratedKey = StorageKeyFor(migrated->id, descriptor);
			local.clearPref(PrefKey(migratedKey));
		}
		break;
	case Override::Enabled:
		local.writePref<bool>(PrefKey(key), true);
		break;
	case Override::Disabled:
		local.writePref<bool>(PrefKey(key), false);
		break;
	default:
		Unexpected("Unknown Ayu chat feature override.");
	}
	NotifyChange(canonical, feature);
}

rpl::producer<Change> Changes() {
	return ChangeEvents.events();
}

void NotifyChange(PeerData *peer, Feature feature) {
	ChangeEvents.fire({
		.peer = peer,
		.feature = feature,
	});
}

} // namespace AyuChatSettings
