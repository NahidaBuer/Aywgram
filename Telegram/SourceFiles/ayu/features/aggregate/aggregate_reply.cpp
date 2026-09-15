#include "ayu/features/aggregate/aggregate_reply.h"

#include "ayu/ayu_settings.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_replies_list.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"
#include "apiwrap.h"

namespace Ayu::Aggregate {

std::shared_ptr<ReplyJob> JobFor(not_null<Main::Session*> session, FilterId folder) {
	static auto jobs = std::map<Main::Session*, std::map<FilterId, std::shared_ptr<ReplyJob>>>();
	const auto [account, added] = jobs.try_emplace(session.get());
	if (added) {
		session->lifetime().add([=] { jobs.erase(session.get()); });
	}
	auto &result = account->second[folder];
	if (!result) {
		result = std::make_shared<ReplyJob>();
	}
	return result;
}

bool ValidReply(not_null<Main::Session*> session, const FullReplyTo &reply) {
	const auto item = session->data().message(reply.messageId);
	return item && item->isRegular() && item->id > 0
		&& !item->history()->peer->isBroadcast()
		&& !item->isEphemeral()
		&& (item->history()->peer->isForum() ? item->topicRootId() : MsgId()) == reply.topicRootId
		&& (item->history()->peer->isMonoforum() ? item->sublistPeerId() : PeerId()) == reply.monoforumPeerId;
}

void ConfirmReplyRead(not_null<Main::Session*> session, const FullReplyTo &reply) {
	const auto &ghost = AyuSettings::ghost(session);
	if (!ghost.sendReadMessages() && !ghost.markReadAfterAction()) {
		return;
	}
	const auto history = session->data().history(reply.messageId.peer);
	const auto till = reply.messageId.msg;
	const auto done = [=] {
		if (reply.monoforumPeerId) {
			if (const auto sublist = history->peer->monoforumSublistFor(reply.monoforumPeerId)) {
				if (sublist->computeInboxReadTillFull() < till) {
					sublist->setInboxReadTill(till, std::nullopt);
					sublist->requestUnreadCount();
				}
				Core::App().notifications().clearIncomingTill(sublist, till);
			}
		} else if (reply.topicRootId && history->peer->isForum()) {
			if (const auto topic = history->peer->forumTopicFor(reply.topicRootId)) {
				const auto replies = topic->replies();
				if (replies->computeInboxReadTillFull() < till) {
					replies->setInboxReadTill(till, std::nullopt);
					replies->requestUnreadCount();
				}
				Core::App().notifications().clearIncomingTill(topic, till);
			}
		} else if (history->inboxReadTillId() < till) {
			history->inboxRead(till);
		}
	};
	const auto doneBool = [=](const MTPBool &result) {
		if (result.type() == mtpc_boolTrue) {
			done();
		}
	};
	if (reply.monoforumPeerId) {
		session->api().request(MTPmessages_ReadSavedHistory(
			history->peer->input(), session->data().peer(reply.monoforumPeerId)->input(), MTP_int(till)
		)).done(doneBool).send();
	} else if (reply.topicRootId && history->peer->isForum()) {
		session->api().request(MTPmessages_ReadDiscussion(
			history->peer->input(), MTP_int(reply.topicRootId), MTP_int(till)
		)).done(doneBool).send();
	} else if (const auto channel = history->peer->asChannel()) {
		session->api().request(MTPchannels_ReadHistory(channel->inputChannel(), MTP_int(till))).done(doneBool).send();
	} else {
		session->api().request(MTPmessages_ReadHistory(history->peer->input(), MTP_int(till))).done([=](const MTPmessages_AffectedMessages &result) {
			session->api().applyAffectedMessages(history->peer, result);
			done();
		}).send();
	}
}

} // namespace Ayu::Aggregate
