/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_widget.h"

#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/media/info_media_inner_widget.h"
#include "info/polls/info_polls_list_widget.h"
#include "info/profile/tabs/adapters/info_profile_tab_media.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/scroll_area.h"
#include "ui/search_field_controller.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace Info::Media {
namespace {

constexpr auto kSharedMediaJumpIdsLimit = 40;

std::optional<Type> TypeForItem(not_null<HistoryItem*> item) {
	if (!item->isRegular() || item->isService() || item->isSponsored()) {
		return std::nullopt;
	}
	const auto types = item->sharedMediaTypes();
	if (types.test(Type::Photo) || types.test(Type::Video)) {
		if (!Info::Profile::MediaTabsExpanded()) {
			return Type::PhotoVideo;
		}
		return types.test(Type::Photo) ? Type::Photo : Type::Video;
	} else if (types.test(Type::File)) {
		return Type::File;
	} else if (types.test(Type::MusicFile)) {
		return Type::MusicFile;
	} else if (types.test(Type::VoiceFile)
		|| types.test(Type::RoundVoiceFile)
		|| types.test(Type::RoundFile)) {
		return Type::RoundVoiceFile;
	} else if (types.test(Type::GIF)) {
		return Type::GIF;
	} else if (types.test(Type::Poll)) {
		return Type::Poll;
	} else if (types.test(Type::Link)) {
		return Type::Link;
	}
	return std::nullopt;
}

} // namespace

std::optional<int> TypeToTabIndex(Type type) {
	switch (type) {
	case Type::Photo: return 0;
	case Type::Video: return 1;
	case Type::File: return 2;
	}
	return std::nullopt;
}

Type TabIndexToType(int index) {
	switch (index) {
	case 0: return Type::Photo;
	case 1: return Type::Video;
	case 2: return Type::File;
	}
	Unexpected("Index in Info::Media::TabIndexToType()");
}

tr::phrase<> SharedMediaTitle(Type type) {
	switch (type) {
	case Type::PhotoVideo:
		return tr::lng_media_type_media;
	case Type::Photo:
		return tr::lng_media_type_photos;
	case Type::GIF:
		return tr::lng_media_type_gifs;
	case Type::Video:
		return tr::lng_media_type_videos;
	case Type::MusicFile:
		return tr::lng_media_type_songs;
	case Type::File:
		return tr::lng_media_type_files;
	case Type::RoundVoiceFile:
		return tr::lng_media_type_audios;
	case Type::Link:
		return tr::lng_media_type_links;
	case Type::RoundFile:
		return tr::lng_media_type_rounds;
	case Type::Poll:
		return tr::lng_media_type_polls;
	}
	Unexpected("Bad media type in Info::TitleValue()");
}

std::shared_ptr<Info::Memento> MementoForItem(
		not_null<HistoryItem*> item) {
	const auto type = TypeForItem(item);
	if (!type) {
		return nullptr;
	}
	if (*type == Type::Poll) {
		auto polls = [&] {
			if (const auto topic = item->topic()) {
				return std::make_shared<Info::Polls::ListMemento>(topic);
			} else if (const auto sublist = item->savedSublist()) {
				return std::make_shared<Info::Polls::ListMemento>(sublist);
			}
			auto peer = item->history()->peer;
			if (const auto migratedTo = peer->migrateTo()) {
				peer = migratedTo;
			}
			const auto migratedFrom = peer->migrateFrom();
			return std::make_shared<Info::Polls::ListMemento>(
				peer,
				migratedFrom ? migratedFrom->id : PeerId());
		}();
		polls->setExactJumpId(item->fullId());
		auto stack = std::vector<std::shared_ptr<ContentMemento>>();
		stack.push_back(std::move(polls));
		return std::make_shared<Info::Memento>(std::move(stack));
	}
	auto media = [&] {
		if (const auto topic = item->topic()) {
			return std::make_shared<Memento>(topic, *type);
		} else if (const auto sublist = item->savedSublist()) {
			return std::make_shared<Memento>(sublist, *type);
		}
		auto peer = item->history()->peer;
		if (const auto migratedTo = peer->migrateTo()) {
			peer = migratedTo;
		}
		const auto migratedFrom = peer->migrateFrom();
		return std::make_shared<Memento>(
			peer,
			migratedFrom ? migratedFrom->id : PeerId(),
			*type);
	}();
	media->setAroundId(item->fullId());
	media->setIdsLimit(kSharedMediaJumpIdsLimit);
	media->setExactJumpId(item->fullId());
	auto stack = std::vector<std::shared_ptr<ContentMemento>>();
	stack.push_back(std::move(media));
	return std::make_shared<Info::Memento>(std::move(stack));
}

Memento::Memento(not_null<Controller*> controller)
: Memento(
	(controller->peer()
		? controller->peer()
		: controller->storiesPeer()
		? controller->storiesPeer()
		: controller->musicPeer()
		? controller->musicPeer()
		: controller->parentController()->session().user()),
	controller->topic(),
	controller->sublist(),
	controller->migratedPeerId(),
	(controller->section().type() == Section::Type::Downloads
		? Type::File
		: controller->section().type() == Section::Type::Stories
		? Type::PhotoVideo
		: controller->section().type() == Section::Type::SavedMusic
		? Type::MusicFile
		: controller->section().mediaType())) {
}

Memento::Memento(not_null<PeerData*> peer, PeerId migratedPeerId, Type type)
: Memento(peer, nullptr, nullptr, migratedPeerId, type) {
}

Memento::Memento(not_null<Data::ForumTopic*> topic, Type type)
: Memento(topic->peer(), topic, nullptr, PeerId(), type) {
}

Memento::Memento(not_null<Data::SavedSublist*> sublist, Type type)
: Memento(sublist->owningHistory()->peer, nullptr, sublist, PeerId(), type) {
}

Memento::Memento(
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist,
	PeerId migratedPeerId,
	Type type)
: ContentMemento(peer, topic, sublist, migratedPeerId)
, _type(type) {
	_searchState.query.type = type;
	_searchState.query.peerId = peer->id;
	_searchState.query.topicRootId = topic ? topic->rootId() : MsgId();
	_searchState.query.monoforumPeerId = sublist
		? sublist->sublistPeer()->id
		: PeerId();
	_searchState.query.migratedPeerId = migratedPeerId;
	if (migratedPeerId) {
		_searchState.migratedList = Storage::SparseIdsList();
	}
}

Section Memento::section() const {
	return Section(_type);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(
		parent,
		controller);
	result->setInternalState(geometry, this);
	return result;
}

Widget::Widget(QWidget *parent, not_null<Controller*> controller)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller));
	_inner->setScrollHeightValue(scrollHeightValue());
	_inner->scrollToRequests(
	) | rpl::on_next([this](Ui::ScrollToRequest request) {
		scrollTo(request);
	}, _inner->lifetime());

	scroll()->setCustomWheelProcess([this](not_null<QWheelEvent*> e) {
		return (e->modifiers() & Qt::ControlModifier)
			&& _inner->processZoomWheel(e);
	});
}

rpl::producer<SelectedItems> Widget::selectedListValue() const {
	return _inner->selectedListValue();
}

void Widget::selectionAction(SelectionAction action) {
	_inner->selectionAction(action);
}

void Widget::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto type = controller()->section().mediaType();
	if (type != Type::Photo
		&& type != Type::Video
		&& type != Type::PhotoVideo) {
		return;
	}
	if (_inner->canZoomIn()) {
		addAction(tr::lng_media_zoom_in(tr::now), [=] {
			_inner->zoomIn();
		}, &st::menuIconZoomIn);
	}
	if (_inner->canZoomOut()) {
		addAction(tr::lng_media_zoom_out(tr::now), [=] {
			_inner->zoomOut();
		}, &st::menuIconZoomOut);
	}
	addAction(tr::lng_calendar(tr::now), [=] {
		controller()->parentController()->showCalendar({
			.chat = Dialogs::Key(
				controller()->session().data().history(
					controller()->key().peer())),
			.date = QDate::currentDate(),
			.mediaPhoto = (type != Type::Video),
			.mediaVideo = (type != Type::Photo),
			.customJump = [=](FullMsgId id, Fn<void()> close) {
				_inner->jumpToMessage(id);
				close();
			},
		});
	}, &st::menuIconSchedule);
}

bool Widget::processZoomKey(not_null<QKeyEvent*> e) {
	if (!(e->modifiers() & Qt::ControlModifier)) {
		return false;
	}
	const auto key = e->key();
	if (key == Qt::Key_Plus || key == Qt::Key_Equal) {
		_inner->zoomIn();
		return true;
	} else if (key == Qt::Key_Minus || key == Qt::Key_Underscore) {
		_inner->zoomOut();
		return true;
	}
	return false;
}

rpl::producer<QString> Widget::title() {
	if (controller()->key().peer()->sharedMediaInfo() && isStackBottom()) {
		return tr::lng_profile_shared_media();
	}
	return SharedMediaTitle(controller()->section().mediaType())();
}

void Widget::setIsStackBottom(bool isStackBottom) {
	ContentWidget::setIsStackBottom(isStackBottom);
	_inner->setIsStackBottom(isStackBottom);
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (const auto mediaMemento = dynamic_cast<Memento*>(memento.get())) {
		if (_inner->showInternal(mediaMemento)) {
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(controller());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	if (const auto id = memento->takeExactJumpId()) {
		_inner->jumpToMessage(id);
	}
}

} // namespace Info::Media
