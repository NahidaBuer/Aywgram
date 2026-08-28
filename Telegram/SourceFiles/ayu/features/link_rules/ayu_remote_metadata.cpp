#include "ayu/features/link_rules/ayu_remote_metadata.h"

#include "apiwrap.h"
#include "ayu/features/link_rules/ayu_link_rules.h"
#include "base/timer.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QRegularExpression>

#include <cstring>

namespace Ayu::RemoteMetadata {
namespace {

constexpr auto kMaximumParts = 32;
constexpr auto kMaximumPayloadSize = 128 * 1024;

struct PayloadManifest {
	std::vector<MsgId> messageIds;
	int parts = 0;
	QByteArray sha256;
};

struct Manifest {
	uint64 revision = 0;
	PayloadManifest pagePreview;
	PayloadManifest inlineBots;
};

rpl::lifetime Lifetime;
base::Timer PollTimer;
bool Requesting = false;
std::vector<Fn<void(bool, QString)>> Callbacks;

QString MessageText(const MTPMessage &message) {
	return message.match(
		[](const MTPDmessage &data) { return qs(data.vmessage()); },
		[](const MTPDmessageService &) { return QString(); },
		[](const MTPDmessageEmpty &) { return QString(); });
}

MsgId MessageId(const MTPMessage &message) {
	return message.match([](const auto &data) {
		return MsgId(data.vid().v);
	});
}

template <typename Callback>
void EnumerateMessages(const MTPmessages_Messages &result, Callback callback) {
	result.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		for (const auto &message : data.vmessages().v) {
			callback(message);
		}
	});
}

std::optional<PayloadManifest> ParsePayloadManifest(
		const nlohmann::json &data) {
	if (!data.is_object()) {
		return std::nullopt;
	}
	auto result = PayloadManifest();
	try {
		result.parts = data.value("parts", 0);
		result.sha256 = QByteArray::fromStdString(
			data.value("sha256", std::string())).toLower();
		const auto ids = data.at("message_ids");
		if (!ids.is_array()) {
			return std::nullopt;
		}
		for (const auto &id : ids) {
			result.messageIds.push_back(MsgId(id.get<int>()));
		}
	} catch (...) {
		return std::nullopt;
	}
	static const auto hashExpression = QRegularExpression(
		u"^[0-9a-f]{64}$"_q);
	if (result.parts <= 0
		|| result.parts > kMaximumParts
		|| result.messageIds.size() != result.parts
		|| !hashExpression.match(QString::fromLatin1(result.sha256)).hasMatch()
		|| ranges::any_of(result.messageIds, [](MsgId id) { return id <= 0; })) {
		return std::nullopt;
	}
	return result;
}

std::optional<Manifest> ParseManifest(const QString &text) {
	constexpr auto marker = "#aywmeta";
	if (!text.startsWith(marker)) {
		return std::nullopt;
	}
	try {
		const auto bytes = text.mid(int(strlen(marker))).toUtf8();
		const auto data = nlohmann::json::parse(
			bytes.constData(),
			bytes.constData() + bytes.size());
		if (data.value("schema", 0) != 1) {
			return std::nullopt;
		}
		auto result = Manifest();
		result.revision = data.value("revision", uint64(0));
		const auto publishedAt = data.value("published_at", int64(0));
		const auto page = ParsePayloadManifest(data.at("pagepreview"));
		const auto bots = ParsePayloadManifest(data.at("inlinebot"));
		if (!result.revision || publishedAt <= 0 || !page || !bots) {
			return std::nullopt;
		}
		result.pagePreview = *page;
		result.inlineBots = *bots;
		return result;
	} catch (...) {
		return std::nullopt;
	}
}

std::optional<QByteArray> AssemblePayload(
		const base::flat_map<MsgId, QString> &messages,
		const PayloadManifest &manifest,
		const QString &marker,
		uint64 revision) {
	auto parts = std::vector<QByteArray>(manifest.parts);
	for (const auto id : manifest.messageIds) {
		const auto i = messages.find(id);
		if (i == end(messages) || !i->second.startsWith(marker)) {
			return std::nullopt;
		}
		try {
			const auto raw = i->second.mid(marker.size()).toUtf8();
			const auto data = nlohmann::json::parse(
				raw.constData(),
				raw.constData() + raw.size());
			const auto part = data.value("part", 0);
			const auto count = data.value("parts", 0);
			if (data.value("schema", 0) != 1
				|| data.value("revision", uint64(0)) != revision
				|| data.value("encoding", std::string()) != "base64url"
				|| part <= 0
				|| part > manifest.parts
				|| count != manifest.parts
				|| !parts[part - 1].isEmpty()) {
				return std::nullopt;
			}
			parts[part - 1] = QByteArray::fromBase64(
				QByteArray::fromStdString(data.value("data", std::string())),
				QByteArray::Base64UrlEncoding
					| QByteArray::AbortOnBase64DecodingErrors);
		} catch (...) {
			return std::nullopt;
		}
	}
	auto result = QByteArray();
	for (const auto &part : parts) {
		if (part.isEmpty() || result.size() + part.size() > kMaximumPayloadSize) {
			return std::nullopt;
		}
		result.append(part);
	}
	const auto hash = QCryptographicHash::hash(
		result,
		QCryptographicHash::Sha256).toHex();
	return (hash == manifest.sha256)
		? std::make_optional(std::move(result))
		: std::nullopt;
}

void Finish(bool success, QString error = {}) {
	Requesting = false;
	if (!success && error.isEmpty()) {
		error = u"Could not refresh AywGram metadata."_q;
	}
	auto callbacks = base::take(Callbacks);
	for (auto &callback : callbacks) {
		if (callback) {
			callback(success, error);
		}
	}
}

void FetchParts(
		not_null<Main::Session*> session,
		not_null<ChannelData*> channel,
		Manifest manifest) {
	auto ids = QVector<MTPInputMessage>();
	for (const auto id : manifest.pagePreview.messageIds) {
		ids.push_back(MTP_inputMessageID(MTP_int(id)));
	}
	for (const auto id : manifest.inlineBots.messageIds) {
		ids.push_back(MTP_inputMessageID(MTP_int(id)));
	}
	const auto weak = base::make_weak(session);
	session->api().request(MTPchannels_GetMessages(
		channel->inputChannel(),
		MTP_vector(ids)
	)).done([=](const MTPmessages_Messages &result) {
		if (!weak) {
			Finish(false);
			return;
		}
		auto messages = base::flat_map<MsgId, QString>();
		EnumerateMessages(result, [&](const MTPMessage &message) {
			messages.emplace(MessageId(message), MessageText(message));
		});
		const auto page = AssemblePayload(
			messages,
			manifest.pagePreview,
			u"#pagepreview"_q,
			manifest.revision);
		const auto bots = AssemblePayload(
			messages,
			manifest.inlineBots,
			u"#inlinebot"_q,
			manifest.revision);
		if (!page || !bots) {
			Finish(false, u"Metadata parts failed validation."_q);
			return;
		}
		auto error = QString();
		const auto applied = LinkRules::ApplyRemotePayloads(
			*page,
			*bots,
			manifest.revision,
			&error);
		Finish(applied, error);
	}).fail([](const MTP::Error &error) {
		Finish(false, error.type());
	}).send();
}

void SearchManifest(
		not_null<Main::Session*> session,
		not_null<ChannelData*> channel) {
	const auto weak = base::make_weak(session);
	session->api().request(MTPmessages_Search(
		MTP_flags(0),
		channel->input(),
		MTP_string(u"#aywmeta"_q),
		MTP_inputPeerEmpty(),
		MTP_inputPeerEmpty(),
		MTPVector<MTPReaction>(),
		MTP_int(0),
		MTP_inputMessagesFilterEmpty(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(10),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		if (!weak) {
			Finish(false);
			return;
		}
		auto manifest = std::optional<Manifest>();
		EnumerateMessages(result, [&](const MTPMessage &message) {
			if (!manifest) {
				manifest = ParseManifest(MessageText(message));
			}
		});
		if (!manifest) {
			Finish(false, u"No valid metadata manifest was found."_q);
			return;
		} else if (manifest->revision <= LinkRules::RemoteRevision()) {
			Finish(true);
			return;
		}
		FetchParts(session, channel, std::move(*manifest));
	}).fail([](const MTP::Error &error) {
		Finish(false, error.type());
	}).send();
}

} // namespace

bool Configured() {
	return kChannelId != 0 && kChannelUsername[0] != '\0';
}

void Refresh(
		not_null<Main::Session*> session,
		Fn<void(bool, QString)> done) {
	if (done) {
		Callbacks.push_back(std::move(done));
	}
	if (Requesting) {
		return;
	} else if (!Configured()) {
		Finish(false, u"Remote metadata service is not configured."_q);
		return;
	} else if (session->isTestMode()) {
		Finish(false, u"Remote metadata is disabled on Test DC."_q);
		return;
	}
	Requesting = true;
	const auto weak = base::make_weak(session);
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(QString::fromLatin1(kChannelUsername)),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		if (!weak) {
			Finish(false);
			return;
		}
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			const auto peer = session->data().peerLoaded(peerFromMTP(data.vpeer()));
			const auto channel = peer ? peer->asChannel() : nullptr;
			if (!channel
				|| peerToChannel(channel->id) != ChannelId(kChannelId)
				|| channel->username().compare(
					QString::fromLatin1(kChannelUsername),
					Qt::CaseInsensitive) != 0) {
				Finish(false, u"Metadata channel identity mismatch."_q);
				return;
			}
			SearchManifest(session, channel);
		});
	}).fail([](const MTP::Error &error) {
		Finish(false, error.type());
	}).send();
}

void Init() {
	LinkRules::Initialize();
	Core::App().domain().activeSessionValue(
	) | rpl::on_next([](Main::Session *session) {
		if (session) {
			Refresh(session);
		}
	}, Lifetime);
	PollTimer.setCallback([] {
		auto &domain = Core::App().domain();
		if (domain.started()) {
			if (const auto session = domain.active().maybeSession()) {
				Refresh(session);
			}
		}
	});
	PollTimer.callEach(6 * 60 * 60 * crl::time(1000));
}

} // namespace Ayu::RemoteMetadata
