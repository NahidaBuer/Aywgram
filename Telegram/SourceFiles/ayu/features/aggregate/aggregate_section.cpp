#include "ayu/features/aggregate/aggregate_section.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QKeyEvent>
#include <QtGui/QWheelEvent>
#include "api/api_sending.h"
#include "ayu/features/aggregate/aggregate_controller.h"
#include "ayu/features/aggregate/aggregate_reply.h"
#include "ayu/features/aggregate/aggregate_settings.h"
#include "base/event_filter.h"
#include "boxes/send_files_box.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/mime_type.h"
#include "data/data_changes.h"
#include "data/data_chat_filters.h"
#include "data/data_chat_participant_status.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_layout.h"
#include "dialogs/ui/dialogs_message_view.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/history_view_item_preview.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "info/global_media/info_global_media_provider.h"
#include "info/media/info_media_list_widget.h"
#include "info/media/info_media_widget.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/view/media_view_open_common.h"
#include "menu/menu_send_details.h"
#include "settings/settings_common.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/filter_icons.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/userpic_view.h"
#include "ui/vertical_list.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"

#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

namespace Ayu::Aggregate {
namespace {

using namespace HistoryView;

constexpr auto kSourcePageSize = 50;

const style::icon &CategoryIcon(Category category) {
	switch (category) {
	case Category::Text: return st::menuIconShowAll;
	case Category::Photo: return st::menuIconPhoto;
	case Category::Video: return st::dialogsSearchInMessageTypeVideo;
	case Category::Music: return st::dialogsSearchInMessageTypeMusic;
	case Category::Voice: return st::dialogsSearchInMessageTypeVoice;
	case Category::File: return st::menuIconFile;
	case Category::Link: return st::menuIconLink;
	case Category::Gif: return st::menuIconGif;
	case Category::RoundVideo: return st::dialogsSearchInMessageTypeRoundVideo;
	case Category::Sticker: return st::menuIconStickers;
	case Category::Poll: return st::menuIconCreatePoll;
	case Category::Location: return st::menuIconAddress;
	case Category::Count: break;
	}
	Unexpected("Aggregate category icon.");
}

const style::icon &SourceIcon(SourceType type) {
	switch (type) {
	case SourceType::Channel: return st::menuIconChannel;
	case SourceType::Group: return st::menuIconGroups;
	case SourceType::Private: return st::menuIconProfile;
	case SourceType::Bot: return st::menuIconBot;
	}
	Unexpected("Aggregate source icon.");
}

void PaintFolder(Painter &p, QRect rect, int width,
		not_null<Main::Session*> session, FilterId folder) {
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowActiveTextFg);
	p.drawEllipse(style::rtlrect(rect, width));
	auto icon = &st::aggregateFolderIcon;
	for (const auto &filter : session->data().chatsFilters().list()) {
		if (filter.id() == folder) {
			icon = Ui::LookupFilterIcon(Ui::ComputeFilterIcon(filter)).userpic;
			break;
		}
	}
	icon->paint(p,
		rect.x() + (rect.width() - icon->width()) / 2,
		rect.y() + (rect.height() - icon->height()) / 2,
		width, st::activeButtonFg->c);
}

void PaintHeader(
		Painter &p,
		int width,
		const std::shared_ptr<Controller> &data,
		FilterId folder,
		not_null<Main::Session*> session) {
	p.fillRect(0, 0, width, st::topBarHeight, st::windowBg);
	const auto size = st::aggregateAvatarSize;
	const auto padding = st::aggregateHeaderPadding;
	PaintFolder(p, QRect(padding, (st::topBarHeight - size) / 2, size, size), width, session, folder);
	const auto left = padding * 2 + size;
	p.setFont(st::semiboldFont);
	p.setPen(st::windowFg);
	p.drawTextLeft(left, st::aggregateHeaderNameTop, width,
		st::semiboldFont->elided(data->title(), std::max(0, width - left - padding)));
	p.setFont(st::dialogsTextFont);
	p.setPen(st::windowSubTextFg);
	const auto status = tr::lng_filters_chats_count(tr::now, lt_count, data->sourceCount());
	p.drawTextLeft(left, st::aggregateHeaderStatusTop, width,
		st::dialogsTextFont->elided(status, std::max(0, width - left - padding)));
}

class FilterAction final : public Ui::Menu::Action {
public:
	FilterAction(not_null<Ui::Menu::Menu*> menu, not_null<QAction*> action, const style::icon &icon);
private:
	void paintEvent(QPaintEvent *e) override;

};

std::map<FilterId, std::shared_ptr<Memento>> &ReadingCache(not_null<Main::Session*> session) {
	static auto accounts = std::map<Main::Session*, std::map<FilterId, std::shared_ptr<Memento>>>();
	const auto [i, added] = accounts.try_emplace(session.get());
	if (added) {
		session->lifetime().add([=] { accounts.erase(session.get()); });
	}
	return i->second;
}

class SidebarMemento;

class SidebarWidget final : public Window::SectionWidget {
public:
	bool showInternal(not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	SidebarWidget(QWidget *parent, not_null<Window::SessionController*> window,
		not_null<SidebarMemento*> memento, bool third);
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	std::optional<int> aggregateFolder() const override { return _folder; }
private:
	void resizeEvent(QResizeEvent *e) override;
	void rebuild();
	std::vector<PeerId> orderedSources() const;
	void moreSources();
	void appendSources(not_null<Ui::VerticalLayout*> content, int from);
public:
	int scrollTop() const { return _scroll->scrollTop(); }
	int sourceLimit() const { return _sourceLimit; }
	void setOwner(std::shared_ptr<Memento> owner) { _owner = std::move(owner); }
private:
	void paintEvent(QPaintEvent *e) override;
	void close();
	QRect floatPlayerAvailableRect() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	FilterId _folder = 0;
	std::shared_ptr<Controller> _data;
	Fn<void(Category)> _media;
	Fn<void()> _closed;
	std::shared_ptr<Memento> _owner;
	bool _third = false;
	object_ptr<Ui::RpWidget> _header;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::ElasticScroll> _scroll;
	QPointer<Ui::VerticalLayout> _sourceContent;
	std::vector<PeerId> _sources;
	std::vector<PeerId> _ordered;
	int _sourceLimit = kSourcePageSize;
	bool _rebuildScheduled = false;
	std::vector<std::uint64_t> _excluded;

};

class SidebarMemento final : public Window::SectionMemento {
public:
	SidebarMemento(FilterId folder, std::shared_ptr<Controller> data,
		Fn<void(Category)> media, Fn<void(Window::SectionWidget*)> created);
	object_ptr<Window::SectionWidget> createWidget(QWidget *parent,
		not_null<Window::SessionController*> controller, Window::Column column,
		const QRect &geometry) override;
	FilterId folder = 0;
	std::shared_ptr<Controller> data;
	Fn<void(Category)> media;
	Fn<void(Window::SectionWidget*)> created;
	Fn<void()> closed;
	std::shared_ptr<Memento> owner;
	bool standalone = true;

};

class MediaController final : public Info::AbstractController {
public:
	MediaController(
		not_null<Window::SessionController*> window,
		Storage::SharedMediaType type);
	Info::Key key() const override;
	PeerData *migrated() const override;
	Info::Section section() const override;
private:
	Storage::SharedMediaType _type;

};

struct TextSendState {
	std::vector<TextWithTags> parts;
	Data::WebPageDraft webPage;
	int next = 0;
};

void SendNextText(
		std::shared_ptr<TextSendState> state,
		Api::SendAction action,
		Api::SendCompletionPtr completion) {
	if (state->next == state->parts.size()) {
		completion->finishRequest(true);
		return;
	}
	auto message = Api::MessageToSend(action);
	message.textWithTags = state->parts[state->next];
	message.webPage = state->webPage;
	if (state->next + 1 < state->parts.size()) {
		message.webPage.removed = true;
	}
	message.completion = Api::MakeSendCompletion([=](bool success) {
		if (!success) {
			completion->finishRequest(false);
			return;
		}
		++state->next;
		SendNextText(state, action, completion);
	});
	action.history->session().api().sendMessage(std::move(message));
}

class Widget final : public Window::SectionWidget, private WindowListDelegate {
public:
	Widget(QWidget *parent, not_null<Window::SessionController*> window, not_null<Memento*> memento);
	~Widget();
	bool showInternal(not_null<Window::SectionMemento*> memento, const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	std::optional<int> aggregateFolder() const override { return _folder; }
	void restore(not_null<Memento*> memento);

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void doSetInnerFocus() override;
	void showFinishedHook() override;
	QRect floatPlayerAvailableRect() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	void updateGeometry();
	void onScroll();
	void requestMore();
	void updateStatus();
	void finishLatest();
	QString replyFailureText() const;
	void save();
	void refresh();
	void filters(bool chats = false);
	void menu();
	void sources();
	void ensureSidebar();
	void captureReading();
	void finishRestore();

	void showMedia(Category category);
	std::shared_ptr<Media::View::Navigation> mediaNavigation(FullMsgId id, bool stream);
	void closeMedia();
	void open(FullMsgId id);
	void chooseReply(FullReplyTo reply);
	void clearReply();
	void saveDraft();
	void restoreDraft();
	void attach(std::optional<bool> compress);
	void confirmFiles(Ui::PreparedList &&list, FullReplyTo reply, std::optional<TextWithTags> caption = std::nullopt, std::optional<QByteArray> submittedDraft = std::nullopt);
	void sendFiles(std::shared_ptr<Ui::PreparedBundle> bundle, Api::SendOptions options, FullReplyTo reply, std::optional<QByteArray> submittedDraft = std::nullopt);
	void submit(FullReplyTo reply, Api::SendOptions options, int count, Fn<void(Api::SendAction, Api::SendCompletionPtr)> operation, std::optional<QByteArray> submittedDraft = std::nullopt, int sendingCount = 0);

	Context listContext() override { return Context::History; }
	bool listScrollTo(int top, bool syntetic) override;
	void listCancelRequest() override { controller()->showBackFromStack(); }
	void listDeleteRequest() override { ConfirmDeleteSelectedItems(_inner); }
	void listTryProcessKeyInput(not_null<QKeyEvent*> e) override {}
	rpl::producer<Data::MessagesSlice> listSource(Data::MessagePosition around, int before, int after) override;
	bool listAllowsMultiSelect() override { return true; }
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) override { return item->canBeSelected(); }
	bool listIsLessInOrder(not_null<HistoryItem*> a, not_null<HistoryItem*> b) override;
	void listSelectionChanged(SelectedItems &&items) override {}
	void listMarkReadTill(not_null<HistoryItem*> item) override {}
	void listMarkContentsRead(const base::flat_set<not_null<HistoryItem*>> &items) override {}
	bool listMarkingContentRead() override { return false; }
	MessagesBarData listMessagesBar(const std::vector<not_null<Element*>> &items, bool markLastAsRead) override { return {}; }
	void listContentRefreshed() override;
	void listUpdateDateLink(ClickHandlerPtr &link, not_null<Element*> view) override {}
	bool listElementHideReply(not_null<const Element*> view) override { return false; }
	bool listElementShownUnread(not_null<const Element*> view) override { return false; }
	bool listIsGoodForAroundPosition(not_null<const Element*> view) override { return view->data()->isRegular(); }
	void listSendBotCommand(const QString &command, const FullMsgId &context) override { open(context); }
	void listSearch(const QString &query, const FullMsgId &context) override { open(context); }
	void listHandleViaClick(not_null<UserData*> bot) override { controller()->showPeerHistory(bot, Window::SectionShow::Way::Forward); }
	not_null<Ui::ChatTheme*> listChatTheme() override { return _theme.get(); }
	CopyRestrictionType listCopyRestrictionType(HistoryItem *item) override;
	CopyRestrictionType listCopyMediaRestrictionType(not_null<HistoryItem*> item) override;
	CopyRestrictionType listSelectRestrictionType() override { return CopyRestrictionType::None; }
	rpl::producer<Data::AllowedReactions> listAllowedReactionsValue() override { return rpl::single(Data::AllowedReactions()); }
	void listShowPremiumToast(not_null<DocumentData*> document) override {}
	void listOpenPhoto(not_null<PhotoData*> photo, FullMsgId context) override;
	void listOpenDocument(not_null<DocumentData*> document, FullMsgId context, bool showInMediaView) override;
	void listPaintEmpty(Painter &p, const Ui::ChatPaintContext &context) override {}
	QString listElementAuthorRank(not_null<const Element*> view) override { return {}; }
	bool listElementHideTopicButton(not_null<const Element*> view) override { return false; }
	History *listTranslateHistory() override { return nullptr; }
	void listAddTranslatedItems(not_null<TranslateTracker*> tracker) override {}
	Ui::ElasticScroll *listScrollArea() const override { return _scroll.get(); }
	bool listThanosEffectEnabled() const override { return false; }
	bool listShowForumThreadBars() const override { return true; }
	Fn<void()> listOriginalMessageAction(FullMsgId id) override;
	bool listCustomOrder() const override { return true; }
	bool listNewestAtTop() override { return _settings.newestFirst; }
	int listDisplayIndex(FullMsgId id) const override { return _data->indexOf(id); }
	bool listRefreshItemHeader(Element *view, Element *previous) override;

	const FilterId _folder;
	std::shared_ptr<Controller> _data;
	std::shared_ptr<ReplyJob> _job;
	FolderSettings _settings;
	std::shared_ptr<Ui::ChatTheme> _theme;
	std::unique_ptr<Ui::ElasticScroll> _scroll;
	QPointer<ListWidget> _inner;
	object_ptr<Ui::RpWidget> _title;
	object_ptr<Ui::IconButton> _info;
	object_ptr<Ui::IconButton> _filter;
	object_ptr<Ui::IconButton> _chatFilter;
	object_ptr<Ui::IconButton> _more;
	object_ptr<Ui::FlatButton> _status;
	object_ptr<Ui::FlatButton> _edgeStatus;
	object_ptr<Ui::FlatButton> _replyHint;
	base::unique_qptr<Ui::PopupMenu> _menu;
	ListMemento _restore;
	std::optional<ListMemento> _reading;
	std::shared_ptr<Info::Media::Memento> _restoreMedia;
	bool _viewReady = false;
	bool _restoreScheduled = false;
	bool _destroying = false;
	bool _restoring = false;
	bool _geometryUpdating = false;
	bool _newMessages = false;
	bool _loadScheduled = false;
	PagingGate _paging;
	std::optional<std::uint64_t> _latestGeneration;
	bool _latestScheduled = false;
	bool _historyEdgeInSlice = false;
	bool _sidebarRestore = false;
	bool _sidebarOpen = false;
	int _sidebarTop = 0;
	int _sidebarLimit = kSourcePageSize;
	FullReplyTo _reply;
	TextWithTags _draftText;
	std::unique_ptr<ComposeControls> _compose;
	rpl::lifetime _composeLifetime;
	SendPaymentHelper _payment;
	std::uint64_t _replyRevision = 0;
	Fn<void()> _retry;
	QPointer<Window::SectionWidget> _sidebar;
	std::set<FullMsgId> _requestedTopics;
	std::unique_ptr<MediaController> _mediaController;
	std::shared_ptr<Controller> _mediaData;
	Category _mediaCategory = Category::Photo;
	std::unique_ptr<Ui::ElasticScroll> _mediaScroll;
	QPointer<Info::Media::ListWidget> _mediaList;
	rpl::lifetime _mediaLifetime;

};

MediaController::MediaController(
		not_null<Window::SessionController*> window,
		Storage::SharedMediaType type)
: AbstractController(window)
, _type(type) {
}

Info::Key MediaController::key() const {
	return Info::Key(Info::GlobalMedia::Tag(session().user()));
}

PeerData *MediaController::migrated() const {
	return nullptr;
}

Info::Section MediaController::section() const {
	return Info::Section(_type, Info::Section::Type::GlobalMedia);
}

bool Widget::listIsLessInOrder(not_null<HistoryItem*> a, not_null<HistoryItem*> b) {
	return _data->indexOf(a->fullId()) < _data->indexOf(b->fullId());
}

CopyRestrictionType Widget::listCopyRestrictionType(HistoryItem *item) {
	return item ? CopyRestrictionTypeFor(item->history()->peer, item) : CopyRestrictionType::None;
}

CopyRestrictionType Widget::listCopyMediaRestrictionType(not_null<HistoryItem*> item) {
	return CopyMediaRestrictionTypeFor(item->history()->peer, item);
}

Fn<void()> Widget::listOriginalMessageAction(FullMsgId id) {
	return crl::guard(this, [=] { open(id); });
}

FilterAction::FilterAction(not_null<Ui::Menu::Menu*> menu,
		not_null<QAction*> action, const style::icon &icon)
: Action(menu, st::aggregateFilterMenu, action, &icon, &icon) {
}

void FilterAction::paintEvent(QPaintEvent *e) {
	Action::paintEvent(e);
	if (action()->isChecked()) {
		Painter p(this);
		st::dialogsSearchInCheck.paint(p, st::menuWithIcons.itemIconPosition, width());
	}
}

SidebarMemento::SidebarMemento(FilterId folder, std::shared_ptr<Controller> data,
		Fn<void(Category)> media, Fn<void(Window::SectionWidget*)> created)
: folder(folder), data(std::move(data)), media(std::move(media)), created(std::move(created)) {
}

object_ptr<Window::SectionWidget> SidebarMemento::createWidget(
		QWidget *parent, not_null<Window::SessionController*> controller,
		Window::Column column, const QRect &geometry) {
	if (column != Window::Column::Third && !standalone && owner) {
		return owner->createWidget(parent, controller, column, geometry);
	}
	auto result = object_ptr<SidebarWidget>(parent, controller, this, column == Window::Column::Third);
	result->setGeometry(geometry);
	if (created && column == Window::Column::Third) {
		created(result.data());
	}
	return result;
}

SidebarWidget::SidebarWidget(QWidget *parent,
		not_null<Window::SessionController*> window,
		not_null<SidebarMemento*> memento, bool third)
: SectionWidget(parent, window)
, _folder(memento->folder)
, _data(memento->data)
, _media(memento->media)
, _third(third)
, _header(this)
, _close(this, st::topBarCloseChoose)
, _scroll(this) {
	_owner = memento->owner;
	_closed = memento->closed;
	if (_owner) {
		_sourceLimit = std::max(kSourcePageSize, _owner->sidebarLimit);
	}
	_scroll->scrolls() | rpl::on_next([=] { moreSources(); }, lifetime());
	_close->setClickedCallback([=] { close(); });
	session().data().chatListEntryRefreshes() | rpl::on_next([=](const Data::Session::ChatListEntryRefresh &change) {
		if (change.filterId != _folder || !change || _rebuildScheduled) {
			return;
		}
		_rebuildScheduled = true;
		crl::on_main(this, [=] {
			_rebuildScheduled = false;
			if (_ordered != orderedSources()) {
				rebuild();
			}
		});
	}, lifetime());
	SettingsChanged() | rpl::on_next([=] {
		if (!ReadGlobal().enabled) {
			crl::on_main(this, [=] { close(); });
		}
	}, lifetime());
	_header->paintRequest() | rpl::on_next([=] {
		Painter p(_header);
		PaintHeader(p, _header->width(), _data, _folder, &session());
	}, lifetime());
	_data->changed() | rpl::on_next([=] {
		if (!FolderExists(&session(), _folder)) {
			crl::on_main(this, [=] { close(); });
			return;
		}
		_header->update();
		if (_sources != _data->sources() || _excluded != ReadFolder(&session(), _folder).excluded) {
			rebuild();
		}
	}, lifetime());
	rebuild();
}

bool SidebarWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	const auto sidebar = dynamic_cast<SidebarMemento*>(memento.get());
	if (!sidebar || sidebar->folder != _folder || sidebar->data != _data) {
		return false;
	}
	_media = sidebar->media;
	_owner = sidebar->owner;
	_closed = sidebar->closed;
	if (_third && sidebar->created) {
		sidebar->created(this);
	}
	return true;
}

void SidebarWidget::close() {
	if (_closed) _closed();
	if (_third) {
		controller()->closeThirdSection();
	} else {
		controller()->showBackFromStack();
	}
}

std::shared_ptr<Window::SectionMemento> SidebarWidget::createMemento() {
	auto result = std::make_shared<SidebarMemento>(_folder, _data, nullptr, nullptr);
	result->owner = _owner ? std::make_shared<Memento>(*_owner) : std::make_shared<Memento>(_folder, _data);
	result->owner->sidebarOpen = true;
	result->owner->sidebarTop = _scroll->scrollTop();
	result->owner->sidebarLimit = _sourceLimit;
	result->standalone = false;
	return result;
}

void SidebarWidget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
	} else {
		Painter p(this);
		p.fillRect(e->rect(), st::windowBg);
	}
}

void SidebarWidget::resizeEvent(QResizeEvent *e) {
	_header->setGeometry(0, 0, std::max(0, width() - _close->width()), st::topBarHeight);
	_close->moveToRight(0, 0);
	_scroll->setGeometry(0, st::topBarHeight, width(), std::max(0, height() - st::topBarHeight));
	if (_owner && _owner->sidebarTop) {
		_scroll->scrollToY(base::take(_owner->sidebarTop));
	}
	moreSources();
}

QRect SidebarWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

bool SidebarWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

std::vector<PeerId> SidebarWidget::orderedSources() const {
	auto ordered = _data->sources();
	ranges::sort(ordered, [&](PeerId a, PeerId b) {
		const auto ak = session().data().history(a)->sortKeyInChatList(_folder);
		const auto bk = session().data().history(b)->sortKeyInChatList(_folder);
		return (ak != bk) ? (ak > bk) : (a < b);
	});
	return ordered;
}

void SidebarWidget::moreSources() {
	if (_scroll->height() <= 0 || _rebuildScheduled || _sourceLimit >= _sources.size()
		|| _scroll->scrollTopMax() - _scroll->scrollTop() > _scroll->height()) {
		return;
	}
	_rebuildScheduled = true;
	crl::on_main(this, [=] {
		_rebuildScheduled = false;
		const auto from = _sourceLimit;
		_sourceLimit += kSourcePageSize;
		if (_sourceContent) {
			appendSources(_sourceContent.data(), from);
			_sourceContent->resizeToWidth(_scroll->width());
			moreSources();
		}
	});
}

void SidebarWidget::rebuild() {
	const auto top = _scroll->scrollTop();
	_sources = _data->sources();
	_excluded = ReadFolder(&session(), _folder).excluded;
	const auto content = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this));
	const auto conversation = Settings::AddButtonWithIcon(content,
		tr::ayu_AggregateConversationView(), st::settingsButton, { &st::menuIconChatBubble });
	conversation->setClickedCallback([=] {
		if (_third && _media) {
			_media(Category::Count);
		} else {
			const auto memento = _owner ? std::make_shared<Memento>(*_owner) : std::make_shared<Memento>(_folder, _data);
			memento->sidebarOpen = false;
			memento->intent = NavigationIntent::Conversation;
			memento->mediaCategory.reset();
			memento->media.reset();
			controller()->showSection(memento, Window::SectionShow::Way::Backward);
		}
	});
	for (const auto category : { Category::Photo, Category::Video, Category::File,
			Category::Music, Category::Voice, Category::Link, Category::Gif }) {
		const auto button = Settings::AddButtonWithIcon(content,
			rpl::single(CategoryLabel(category)), st::settingsButton, { &CategoryIcon(category) });
		button->setClickedCallback([=] {
			if (_third && _media) {
				_media(category);
			} else {
				const auto memento = _owner ? std::make_shared<Memento>(*_owner) : std::make_shared<Memento>(_folder, _data);
				memento->mediaCategory = category;
				controller()->showSection(memento, Window::SectionShow::Way::Backward);
			}
		});
	}

	Ui::AddDivider(content);
	Ui::AddSubsectionTitle(content, tr::ayu_AggregateIncludeSource());
	_ordered = orderedSources();
	_sourceContent = content;
	appendSources(content, 0);

	_scroll->widthValue() | rpl::on_next([=](int width) {
		content->resizeToWidth(width);
	}, content->lifetime());
	_scroll->scrollToY(top);
}

void SidebarWidget::appendSources(not_null<Ui::VerticalLayout*> content, int from) {
	auto shown = 0;
	for (const auto peer : _ordered) {
		if (shown < from) { ++shown; continue; }
		if (shown++ >= _sourceLimit) {
			break;
		}
		const auto data = session().data().peer(peer);

		const auto excluded = ranges::contains(_excluded, peer.value);
		const auto toggle = Settings::AddButtonWithIcon(content,
			rpl::single(QString()), st::aggregateSourceButton);
		toggle->toggleOn(rpl::single(!excluded));
		toggle->setClickedCallback([=] {
			crl::on_main(this, [=] { _data->exclude(peer, !excluded); });
		});
		const auto name = Ui::CreateChild<Ui::RippleButton>(toggle, st::dialogsRipple);
		struct RowState {
			Ui::PeerUserpicView userpic;
			Dialogs::Ui::MessageView preview;
		};
		const auto state = name->lifetime().make_state<RowState>();
		const auto userpic = &state->userpic;
		const auto preview = &state->preview;
		const auto history = session().data().history(peer);
		session().data().chatListEntryRefreshes() | rpl::on_next([=](const Data::Session::ChatListEntryRefresh &change) {
			if (change.key.history() == history) {
				if (const auto item = history->chatListMessage()) {
					preview->itemInvalidated(item);
				}
				name->update();
			}
		}, name->lifetime());
		session().changes().messageUpdates(Data::MessageUpdate::Flag::Edited
			| Data::MessageUpdate::Flag::Destroyed | Data::MessageUpdate::Flag::DialogRowRepaint)
		| rpl::on_next([=](const Data::MessageUpdate &update) {
			if (update.item->history() == history) {
				preview->itemInvalidated(update.item);
				name->update();
			}
		}, name->lifetime());
		data->loadUserpic();
		name->setAccessibleName(data->name());
		toggle->setAccessibleName(tr::ayu_AggregateIncludeSource(tr::now) + u": "_q + data->name());
		name->setClickedCallback([=] {
			controller()->showPeerHistory(peer, Window::SectionShow::Way::Forward);
		});
		toggle->sizeValue() | rpl::on_next([=](QSize size) {
			name->setGeometry(0, 0, std::max(0, size.width() - st::aggregateSourceToggleSpace), size.height());
		}, name->lifetime());
		name->paintRequest() | rpl::on_next([=] {
			Painter p(name);
			name->paintRipple(p, 0, 0);
			const auto size = st::aggregateSourceAvatar;
			const auto padding = st::aggregateSourcePadding;
			data->paintUserpicLeft(p, *userpic, padding, (name->height() - size) / 2, name->width(), size);
			auto left = padding * 2 + size;
			if (const auto icon = Dialogs::Ui::ChatTypeIcon(data)) {
				icon->paint(p, left, st::aggregateSourceNameTop, name->width());
				left += icon->width() + st::dialogsChatTypeSkip;
			}
			p.setFont(st::normalFont);
			p.setPen(st::windowFg);
			p.drawTextLeft(left, st::aggregateSourceNameTop, name->width(),
				st::normalFont->elided(data->name(), std::max(0, name->width() - left)));
			if (const auto item = history->chatListMessage()) {
				if (!preview->prepared(item, nullptr, nullptr)) {
					preview->prepare(item, nullptr, nullptr, crl::guard(name, [=] { name->update(); }), {});
				}
				const auto textLeft = padding * 2 + size;
				preview->paint(p, QRect(textLeft, st::aggregateSourcePreviewTop,
					std::max(0, name->width() - textLeft - padding), st::dialogsTextFont->height), {
					.st = &st::defaultDialogRow,
					.currentBg = st::windowBg->b,
					.now = crl::now(),
					.width = name->width(),
				});
			}
		}, name->lifetime());
		session().downloaderTaskFinished() | rpl::on_next([=] { name->update(); }, name->lifetime());
		session().changes().peerUpdates(data, Data::PeerUpdate::Flag::Name | Data::PeerUpdate::Flag::Photo)
		| rpl::on_next([=] {
			data->loadUserpic();
			name->setAccessibleName(data->name());
			name->update();
		}, name->lifetime());
		name->show();
	}
}

Widget::Widget(QWidget *parent, not_null<Window::SessionController*> window, not_null<Memento*> memento)
: SectionWidget(parent, window)
, WindowListDelegate(window)
, _folder(memento->folder)
, _data(memento->data ? memento->data : ControllerFor(&window->session(), _folder))
, _job(JobFor(&window->session(), _folder))
, _settings(ReadFolder(&window->session(), _folder))
, _scroll(std::make_unique<Ui::ElasticScroll>(this, window->chatStyle()->value(lifetime(), st::historyScroll)))
, _title(this)
, _info(this, st::topBarInfo)
, _filter(this, st::aggregateFilter)
, _chatFilter(this, st::aggregateChatFilter)
, _more(this, st::topBarMenuToggle)
, _status(this, QString(), st::historyComposeButton)
, _edgeStatus(this, QString(), st::historyComposeButton)
, _replyHint(this, tr::ayu_AggregateChooseReply(tr::now), st::historyComposeButton) {
	Window::ChatThemeValueFromPeer(window, session().user()) | rpl::on_next([=](std::shared_ptr<Ui::ChatTheme> theme) {
		_theme = std::move(theme);
		window->setChatStyleTheme(_theme);
	}, lifetime());
	_scroll->setHandleTouch(false);
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(this, &session(), static_cast<ListDelegate*>(this)));
	_inner->scrollKeyEvents() | rpl::on_next([=](not_null<QKeyEvent*> event) {
		_scroll->keyPressEvent(event);
	}, lifetime());
	_scroll->scrolls() | rpl::on_next([=] { onScroll(); }, lifetime());
	const auto gesture = [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Wheel || event->type() == QEvent::KeyPress
			|| event->type() == QEvent::TouchUpdate || event->type() == QEvent::MouseButtonPress) {
			_paging.reset();
		}
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(this, _scroll.get(), gesture);
	base::install_event_filter(this, _inner.data(), gesture);
	_scroll->setBottomContentRequest([=] {
		if (_settings.newestFirst) {
			requestMore();
		}
		return false;
	});
	const auto olderGesture = [=](not_null<QEvent*> event) {
		if (_settings.newestFirst || _scroll->scrollTop() > 0) {
			return base::EventFilterResult::Continue;
		}
		const auto older = event->type() == QEvent::Wheel
			? static_cast<QWheelEvent*>(event.get())->angleDelta().y() > 0
			: event->type() == QEvent::KeyPress
			? (static_cast<QKeyEvent*>(event.get())->key() == Qt::Key_Up
				|| static_cast<QKeyEvent*>(event.get())->key() == Qt::Key_PageUp)
			: event->type() == QEvent::TouchUpdate;
		if (older) {
			requestMore();
		}
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(this, _scroll.get(), olderGesture);
	base::install_event_filter(this, _inner.data(), olderGesture);
	_filter->setClickedCallback([=] { filters(); });
	_chatFilter->setClickedCallback([=] { filters(true); });
	_chatFilter->setAccessibleName(tr::lng_filters_all(tr::now));
	_more->setClickedCallback([=] { menu(); });
	_info->setClickedCallback([=] { sources(); });
	_info->setAccessibleName(tr::ayu_AggregateSources(tr::now));
	_filter->setAccessibleName(tr::ayu_AggregateTitle(tr::now));
	_title->paintRequest() | rpl::on_next([=] {
		Painter p(_title);
		PaintHeader(p, _title->width(), _data, _folder, &session());
	}, _title->lifetime());
	_status->setClickedCallback([=] { closeMedia(); refresh(); });
	_edgeStatus->setClickedCallback([=] {
		const auto data = _mediaData ? _mediaData : _data;
		if (data->failed()) {
			_paging.reset();
			data->retry();
		}
	});
	_data->newMessages() | rpl::on_next([=] {
		_newMessages = true;
		updateStatus();
	}, lifetime());
	_data->changed() | rpl::on_next([=] {
		const auto settings = ReadFolder(&session(), _folder);
		if (_settings.categories != settings.categories || _settings.sources != settings.sources
			|| _settings.unmutedOnly != settings.unmutedOnly || _settings.excluded != settings.excluded) {
			_paging.reset();
		}
		_settings.categories = settings.categories;
		_settings.sources = settings.sources;
		_settings.unmutedOnly = settings.unmutedOnly;
		_settings.newestFirst = settings.newestFirst;
		_settings.excluded = settings.excluded;
		if (!FolderExists(&session(), _folder)) {
			crl::on_main(this, [=] { controller()->showBackFromStack(); });
			return;
		}

		_title->update();
		_newMessages = _data->hasNewMessages();
		if (_mediaData) {
			return;
		}
		updateStatus();
	}, lifetime());
	_inner->replyToMessageRequested() | rpl::on_next([=](ListWidget::ReplyToMessageRequest request) {
		chooseReply(request.to);
	}, lifetime());
	_inner->showMessageRequested() | rpl::on_next([=](FullMsgId id) {
		open(id);
	}, lifetime());
	_inner->editMessageRequested() | rpl::on_next([=](FullMsgId id) {
		open(id);
	}, lifetime());
	session().changes().messageUpdates(Data::MessageUpdate::Flag::Destroyed) | rpl::on_next([=](const Data::MessageUpdate &update) {
		if (update.item->fullId() == _reply.messageId) {
			const auto revision = _replyRevision;
			crl::on_main(this, [=] {
				if (revision == _replyRevision) {
					clearReply();
				}
			});
		}
	}, lifetime());
	SettingsChanged() | rpl::on_next([=] {
		if (!ReadGlobal().enabled) {
			crl::on_main(this, [=] { controller()->showBackFromStack(); });
		}
	}, lifetime());
	_data->resume();
	_title->update();
	_newMessages = _data->hasNewMessages();
	_sidebarRestore = memento->sidebarOpen;
	_sidebarOpen = memento->sidebarOpen;
	_sidebarTop = memento->sidebarTop;
	_sidebarLimit = std::max(kSourcePageSize, memento->sidebarLimit);
	restore(memento);
	restoreDraft();
	if (memento->mediaCategory) {
		showMedia(*memento->mediaCategory);
		_restoreMedia = memento->media;
	}
	if (memento->intent == NavigationIntent::RefreshLatest) {
		closeMedia();
		refresh();
	}
	updateStatus();
	const auto restoreRetry = [=] {
		_retry = crl::guard(this, [=] {
			if (_job->operation) {
				submit(_job->reply, _job->options, _job->count, _job->operation, _job->draft, _job->sendingCount);
			}
		});
	};
	if (_job->failed) {
		restoreRetry();
		_replyHint->setText(replyFailureText());
		_replyHint->setClickedCallback([=] { if (_retry) _retry(); });
	}
	_job->completed.events() | rpl::on_next([=](bool success) {
		if (!success) {
			restoreRetry();
			_replyHint->setText(replyFailureText());
			_replyHint->setClickedCallback([=] { if (_retry) _retry(); });
			return;
		}
		_retry = nullptr;
		_inner->refreshViewer();
		controller()->showToast(tr::ayu_AggregateSent(tr::now));
		if (_settings.draft == _job->draft) {
			const auto revision = _replyRevision;
			crl::on_main(this, [=] {
				if (_replyRevision == revision) {
					if (_compose) {
						_compose->clear();
					}
					_draftText = {};
					clearReply();
				}
			});
		}
	}, lifetime());
}

Widget::~Widget() {
	_destroying = true;
	const auto state = std::static_pointer_cast<Memento>(createMemento());
	state->data.reset();
	if (FolderExists(&session(), _folder)) {
		ReadingCache(&session())[_folder] = state;
	}
	_mediaLifetime.destroy();
	saveDraft();
	save();
}

void Widget::restore(not_null<Memento*> memento) {
	_restore = memento->list;
	if (!memento->hasReadingState && !_restore.scrollTopState().item.fullId && _settings.anchor.message) {
		_restore.setAroundPosition({ FullId(_settings.anchor), TimeId(0) });
		_restore.setScrollTopState({ { FullId(_settings.anchor), TimeId(0) }, _settings.anchorShift });
	}
	_restoring = true;
	_reading = _restore;
	if (_restoring && _data->indexOf(_restore.scrollTopState().item.fullId) < 0
		&& _data->anchorUnavailable(_restore.scrollTopState().item.fullId)
		&& _restore.scrollTopState().item.date && _data->loadedCount()) {
		const auto anchor = _restore.scrollTopState();
		const auto savedDate = anchor.item.date;
		auto nearest = FullMsgId();
		auto distance = std::numeric_limits<int64>::max();
		for (const auto &segment : _data->segments()) {
			for (const auto &record : segment.records) {
				const auto item = session().data().message(FullId(record.id));
				if (item && std::abs(int64(item->date()) - savedDate) < distance) {
					nearest = item->fullId();
					distance = std::abs(int64(item->date()) - savedDate);
				}
			}
		}
		if (nearest) {
			_restore.setAroundPosition({ nearest, savedDate });
			_restore.setScrollTopState({ { nearest, savedDate }, anchor.shift });
		}
	}
	if (const auto anchor = _restore.scrollTopState().item.fullId) {
		_restore.setAroundPosition(_restore.scrollTopState().item);
	}
	finishRestore();
}

bool Widget::showInternal(not_null<Window::SectionMemento*> memento, const Window::SectionShow &params) {
	const auto aggregate = dynamic_cast<Memento*>(memento.get());
	if (!aggregate || aggregate->folder != _folder) {
		return false;
	}
	if (aggregate->intent == NavigationIntent::RefreshLatest) {
		closeMedia();
		refresh();
		return true;
	}
	if (aggregate->intent == NavigationIntent::Conversation) {
		closeMedia();
		return true;
	}
	closeMedia();
	if (aggregate->data && aggregate->data != _data) {
		return false;
	}
	_sidebarRestore = aggregate->sidebarOpen;
	_sidebarOpen = aggregate->sidebarOpen;
	_sidebarTop = aggregate->sidebarTop;
	_sidebarLimit = std::max(kSourcePageSize, aggregate->sidebarLimit);
	restore(aggregate);
	if (aggregate->mediaCategory) {
		showMedia(*aggregate->mediaCategory);
		_restoreMedia = aggregate->media;
	}
	return true;
}

std::shared_ptr<Window::SectionMemento> Widget::createMemento() {
	auto result = std::make_shared<Memento>(_folder, _data);
	captureReading();
	result->list = _reading.value_or(_restore);
	result->hasReadingState = _reading.has_value();
	result->sidebarOpen = _sidebarOpen;
	result->sidebarTop = _sidebarTop;
	result->sidebarLimit = _sidebarLimit;
	if (_mediaData) {
		result->mediaCategory = _mediaCategory;
		result->media = std::make_shared<Info::Media::Memento>(session().user(), PeerId(), _mediaController->section().mediaType());
		_mediaList->saveState(result->media.get());
	}
	if (const auto sidebar = dynamic_cast<SidebarWidget*>(_sidebar.data())) {
		result->sidebarOpen = true;
		result->sidebarTop = sidebar->scrollTop();
		result->sidebarLimit = sidebar->sourceLimit();
		sidebar->setOwner(std::make_shared<Memento>(*result));
	}
	save();
	return result;
}

void Widget::save() {
	if (!_reading || _restoring || _latestGeneration) {
		return;
	}
	const auto &memento = *_reading;
	auto settings = ReadFolder(&session(), _folder);
	settings.anchor = Key(memento.scrollTopState().item.fullId);
	settings.anchorShift = memento.scrollTopState().shift;
	settings.snapshot = _data->snapshot();
	WriteFolder(&session(), _folder, settings, false);
}

void Widget::refresh() {
	_newMessages = false;
	_restoring = false;
	_restore = ListMemento(Data::MaxMessagePosition);
	_reading = _restore;
	_paging.reset();
	_settings = ReadFolder(&session(), _folder);
	_settings.anchor = {};
	_settings.snapshot = 0;
	WriteFolder(&session(), _folder, _settings, false);
	_latestGeneration.reset();
	_data->refresh();
	_latestGeneration = _data->generation();
	_inner->restoreState(&_restore);
	updateStatus();
	finishLatest();
}

void Widget::finishLatest() {
	if (!_latestGeneration || _latestScheduled || _data->loading()) return;
	const auto generation = *_latestGeneration;
	_latestScheduled = true;
	crl::on_main(this, [=] {
		_latestScheduled = false;
		if (!_latestGeneration || *_latestGeneration != generation) {
			finishLatest();
			return;
		}
		if (_data->generation() != generation) {
			_latestGeneration.reset();
			return;
		}
		if (_data->loading()) return;
		_latestGeneration.reset();
		auto latest = ListMemento(Data::MaxMessagePosition);
		_inner->restoreState(&latest);
		_scroll->scrollToY(_settings.newestFirst ? 0 : _scroll->scrollTopMax());
		captureReading();
		save();
	});
}

void Widget::updateStatus() {
	if (!_inner) return;
	const auto data = _mediaData ? _mediaData : _data;

	const auto empty = _mediaData
		? _mediaData->mediaSlice(_mediaCategory, Data::MaxMessagePosition, 16).ids.empty()
		: !data->loadedCount();
	_status->setText(tr::ayu_AggregateNewMessages(tr::now));
	_status->setVisible(_newMessages);
	_edgeStatus->setText(data->loading() ? tr::lng_contacts_loading(tr::now)
		: data->failed() ? tr::ayu_AggregatePartial(tr::now)
		: empty ? tr::lng_search_no_results(tr::now)
		: tr::ayu_AggregateEnd(tr::now));
	_edgeStatus->setVisible(data->loading() || data->failed() || empty || !data->hasMore());
	updateGeometry();
	finishLatest();
	finishRestore();
}

void Widget::resizeEvent(QResizeEvent *e) {
	updateGeometry();
}

void Widget::updateGeometry() {
	if (!_inner || _geometryUpdating) {
		return;
	}
	_geometryUpdating = true;
	const auto contentWidth = width();
	_more->moveToRight(0, 0);
	_info->moveToRight(_more->width() + st::topBarSkip, 0);
	_filter->moveToRight(_more->width() + st::topBarSkip + _info->width(), 0);
	_chatFilter->moveToRight(_more->width() + st::topBarSkip + _info->width() + _filter->width(), 0);
	_title->resize(std::max(0, width() - _more->width() - st::topBarSkip - _filter->width() - _info->width() - _chatFilter->width()), st::topBarHeight);
	_title->move(0, 0);
	_status->resizeToWidth(width());
	_status->move(0, _title->height());
	_replyHint->resizeToWidth(contentWidth);
	const auto composeHeight = _compose ? _compose->heightCurrent() : 0;
	_replyHint->move(0, height() - _replyHint->height() - composeHeight);
	if (_compose) {
		_compose->resizeToWidth(contentWidth);
		_compose->move(0, height() - composeHeight);
	}
	_edgeStatus->resizeToWidth(contentWidth);
	_edgeStatus->move(0, _replyHint->y() - (_edgeStatus->isHidden() ? 0 : _edgeStatus->height()));
	const auto top = _title->height() + (_status->isHidden() ? 0 : _status->height());
	_scroll->setGeometry(0, top, contentWidth, std::max(0, _edgeStatus->y() - top));
	_inner->resizeToWidth(contentWidth, _scroll->height());

	if (_mediaScroll) {
		_mediaScroll->setGeometry(_scroll->geometry());
		_mediaList->resizeToWidth(contentWidth);
	}
	_geometryUpdating = false;
	onScroll();
}

QString Widget::replyFailureText() const {
	return session().data().peer(_job->reply.messageId.peer)->name()
		+ u" · "_q + tr::ayu_AggregateSendFailed(tr::now);
}

void Widget::requestMore() {
	const auto data = _mediaData ? _mediaData : _data;
	if (_loadScheduled || _latestGeneration || data->loading() || !data->hasMore() || data->failed()) {
		return;
	}
	if (!_paging.take(true, data->loading(), data->failed(), data->hasMore())) return;
	_loadScheduled = true;
	const auto generation = data->generation();
	crl::on_main(this, [=] {
		_loadScheduled = false;
		if (data == (_mediaData ? _mediaData : _data) && data->generation() == generation && !_latestGeneration) {
			data->loadMore();
		}
	});
}

void Widget::onScroll() {
	if (_geometryUpdating) {
		return;
	}
	const auto top = _scroll->scrollTop();
	_inner->setVisibleTopBottom(top, top + _scroll->height());
	if (_mediaScroll) {
		const auto mediaTop = _mediaScroll->scrollTop();
		_mediaList->setVisibleTopBottom(mediaTop, mediaTop + _mediaScroll->height());
		if (_mediaScroll->scrollTopMax() - mediaTop <= _mediaScroll->height()) requestMore();
	} else if (!_latestGeneration && _historyEdgeInSlice) {
		const auto distance = _settings.newestFirst ? _scroll->scrollTopMax() - top : top;
		if (distance <= _scroll->height()) requestMore();
	}
	captureReading();
}

void Widget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
	} else {
		SectionWidget::PaintBackground(controller(), _theme.get(), this, e->rect());
		Painter p(this);
		p.fillRect(0, 0, width(), st::topBarHeight, st::windowBg);
	}
}

void Widget::doSetInnerFocus() { _inner->setFocus(); }
void Widget::showFinishedHook() {
	_inner->showFinished();
	_viewReady = true;
	updateStatus();
	finishRestore();
}

void Widget::captureReading() {
	if (!_viewReady || _destroying || _restoring || _latestGeneration
		|| _geometryUpdating || isHidden() || animatingShow() || _mediaScroll
		|| !_data->loadedCount() || _scroll->height() <= 0) {
		return;
	}
	auto state = ListMemento(Data::MaxMessagePosition);
	_inner->saveState(&state);
	if (const auto anchor = state.scrollTopState().item.fullId) {
		state.setAroundPosition(state.scrollTopState().item);
	} else {
		state.setAroundPosition(Data::MaxMessagePosition);
	}
	_reading = std::move(state);
}

void Widget::finishRestore() {
	if (!_viewReady || _destroying || _restoreScheduled || !_restoring
		|| isHidden() || animatingShow() || _scroll->height() <= 0) {
		return;
	}
	_restoreScheduled = true;
	crl::on_main(this, [=] {
		_restoreScheduled = false;
		if (!_restoring || isHidden() || animatingShow()) return;
		if (base::take(_sidebarRestore)) {
			if (controller()->canShowThirdSection()) ensureSidebar();
			finishRestore();
			return;
		}
		updateGeometry();
		const auto anchor = _restore.scrollTopState().item.fullId;
		const auto action = ReadingRestoreAction(
			!anchor || _data->indexOf(anchor) >= 0,
			_data->anchorUnavailable(anchor),
			_data->loading(),
			_data->failed(),
			_data->hasMore());
		if (action == RestoreAction::Wait) {
			return;
		} else if (action == RestoreAction::LoadMore) {
			_data->loadMore();
			return;
		}
		if (anchor && _data->indexOf(anchor) < 0) {
			auto nearest = Data::MessagePosition();
			auto distance = std::numeric_limits<int64>::max();
			for (const auto &segment : _data->segments()) {
				for (const auto &record : segment.records) {
					if (record.id.peer != anchor.peer.value) {
						continue;
					}
					const auto delta = std::abs(record.id.message - anchor.msg.bare);
					if (delta < distance) {
						distance = delta;
						nearest = { FullId(record.id), TimeId(record.date) };
					}
				}
			}
			if (nearest.fullId) {
				const auto shift = _restore.scrollTopState().shift;
				_restore.setAroundPosition(nearest);
				_restore.setScrollTopState({ nearest, shift });
			}
		}
		_restoring = false;
		_inner->restoreState(&_restore);
		if (!anchor) _scroll->scrollToY(_settings.newestFirst ? 0 : _scroll->scrollTopMax());
		if (_restoreMedia && _mediaList) {
			_mediaList->restoreState(_restoreMedia.get());
			_restoreMedia.reset();
		}
		captureReading();
		save();
	});
}

QRect Widget::floatPlayerAvailableRect() {
	const auto scroll = _mediaScroll ? _mediaScroll.get() : _scroll.get();
	return mapToGlobal(scroll->geometry());
}

bool Widget::floatPlayerHandleWheelEvent(QEvent *e) {
	const auto scroll = _mediaScroll ? _mediaScroll.get() : _scroll.get();
	return scroll->viewportEvent(e);
}

bool Widget::listScrollTo(int top, bool syntetic) {
	const auto previous = _scroll->scrollTop();
	_scroll->scrollToY(std::clamp(top, 0, _scroll->scrollTopMax()));
	onScroll();
	return previous != _scroll->scrollTop();
}

rpl::producer<Data::MessagesSlice> Widget::listSource(Data::MessagePosition around, int before, int after) {
	return rpl::single(rpl::empty_value()) | rpl::then(_data->changed()) | rpl::map([=] {
		const auto slice = _data->slice(around.fullId, before, after);
		_historyEdgeInSlice = slice.ids.empty() || (_settings.newestFirst
			? _data->indexOf(slice.ids.back()) + 1 == _data->loadedCount()
			: _data->indexOf(slice.ids.front()) == 0);
		return slice;
	});
}

void Widget::listContentRefreshed() {
	finishLatest();
	finishRestore();
	onScroll();
}

bool Widget::listRefreshItemHeader(Element *view, Element *previous) {
	const auto segment = _data->segmentFor(view->data()->fullId());
	if (!segment || (previous && _data->segmentFor(previous->data()->fullId()) == segment)) {
		view->refreshAggregateBar(nullptr, {}, nullptr);
		return true;
	}
	const auto item = view->data();
	auto thread = static_cast<Data::Thread*>(item->history());
	auto title = item->history()->peer->name();
	if (const auto topic = item->history()->peer->forumTopicFor(item->topicRootId())) {
		thread = topic;
		title += u" / "_q + topic->title();
	} else if (const auto forum = item->history()->asForum(); forum && item->topicRootId()) {
		const auto root = FullMsgId(item->history()->peer->id, item->topicRootId());
		title += u" / #"_q + QString::number(root.msg.bare);
		if (_requestedTopics.insert(root).second) {
			forum->requestTopic(root.msg, crl::guard(this, [=] {
				_inner->refreshViewer();
			}));
		}
	} else if (const auto sublist = item->savedSublist(); sublist && sublist->parentChat()) {
		thread = sublist;
		title += u" / "_q + sublist->sublistPeer()->name();
	}
	view->refreshAggregateBar(thread, title, std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
		open(item->fullId());
	})));
	return true;
}

void Widget::open(FullMsgId id) {
	if (const auto item = session().data().message(id)) {
		save();
		if (const auto topic = item->history()->peer->forumTopicFor(item->topicRootId())) {
			controller()->showTopic(topic, id.msg, Window::SectionShow::Way::Forward);
		} else if (const auto forum = item->history()->asForum(); forum && item->topicRootId()) {
			forum->requestTopic(item->topicRootId(), crl::guard(this, [=] {
				if (const auto current = session().data().message(id)) {
					if (const auto topic = current->history()->peer->forumTopicFor(current->topicRootId())) {
						controller()->showTopic(topic, id.msg, Window::SectionShow::Way::Forward);
					}
				}
			}));
		} else if (const auto sublist = item->savedSublist(); sublist && sublist->parentChat()) {
			controller()->showSublist(sublist, id.msg, Window::SectionShow::Way::Forward);
		} else {
			controller()->showPeerHistory(id.peer, Window::SectionShow::Way::Forward, id.msg);
		}
	}
}

std::shared_ptr<Media::View::Navigation> Widget::mediaNavigation(FullMsgId id, bool stream) {
	const auto original = _data;
	const auto data = original->fork(!stream);
	const auto selection = _settings;
	const auto item = session().data().message(id);
	if (!item || !item->media() || item->media()->webpage()
		|| (!item->media()->photo() && !item->media()->document())) {
		return nullptr;
	}
	const auto described = Describe(item);
	const auto visual = Bit(Category::Photo) | Bit(Category::Video);
	const auto categories = !stream ? Bit(_mediaCategory)
		: (described.categories & visual) ? visual : described.categories;
	const auto folder = _folder;
	const auto window = base::make_weak(controller());
	auto result = std::make_shared<Media::View::Navigation>();
	result->slice = [=] { return data->mediaNavigationSlice(categories, stream); };
	result->changes = [=] { return data->changed(); };
	result->loading = [=] { return data->loading(); };
	result->failed = [=] { return data->failed(); };
	result->loadMore = [=](int) {
		if (data->failed()) {
			data->retry();
		} else {
			data->loadMore();
		}
	};
	result->showMessage = [=](FullMsgId target) {
		if (!window || !ReadGlobal().enabled || !FolderExists(&window->session(), folder)) {
			return;
		}
		auto settings = ReadFolder(&window->session(), folder);
		settings.categories = stream ? selection.categories : 0;
		settings.sources = selection.sources;
		settings.unmutedOnly = selection.unmutedOnly;
		settings.newestFirst = selection.newestFirst;
		WriteFolder(&window->session(), folder, settings, false);
		original->restoreSnapshot(*data);
		const auto memento = std::make_shared<Memento>(folder, original);
		memento->intent = NavigationIntent::LocateMessage;
		memento->list.setAroundPosition({ target, TimeId(0) });
		memento->list.setScrollTopState({ { target, TimeId(0) }, 0 });
		window->showSection(memento);
		if (original->indexOf(target) < 0) {
			original->loadMore();
		}
	};
	const auto overview = !stream ? _mediaCategory
		: (described.categories & Bit(Category::Video)) ? Category::Video
		: (described.categories & Bit(Category::Photo)) ? Category::Photo : Category::File;
	result->showOverview = [=] {
		if (!window || !ReadGlobal().enabled || !FolderExists(&window->session(), folder)) {
			return;
		}
		const auto memento = std::make_shared<Memento>(folder, original);
		memento->mediaCategory = overview;
		window->showSection(memento);
	};
	return result;
}

void Widget::listOpenPhoto(not_null<PhotoData*> photo, FullMsgId context) {
	controller()->openPhoto(photo, { .id = context, .suppressRead = true, .navigation = mediaNavigation(context, true) });
}

void Widget::listOpenDocument(not_null<DocumentData*> document, FullMsgId context, bool showInMediaView) {
	controller()->openDocument(document, showInMediaView, { .id = context, .suppressRead = true, .navigation = mediaNavigation(context, true) });
}

void Widget::saveDraft() {
	if (_compose) {
		_draftText = _compose->fieldForMention()->getTextWithTags();
	}
	auto tags = QJsonArray();
	for (const auto &tag : _draftText.tags) {
		tags.push_back(QJsonObject{
			{ u"offset"_q, tag.offset },
			{ u"length"_q, tag.length },
			{ u"id"_q, tag.id },
		});
	}
	auto settings = ReadFolder(&session(), _folder);
	auto quoteTags = QJsonArray();
	for (const auto &tag : TextUtilities::ConvertEntitiesToTextTags(_reply.quote.entities)) {
		quoteTags.push_back(QJsonObject{
			{ u"offset"_q, tag.offset },
			{ u"length"_q, tag.length },
			{ u"id"_q, tag.id },
		});
	}
	settings.draft = QJsonDocument(QJsonObject{
		{ u"peer"_q, QString::number(_reply.messageId.peer.value) },
		{ u"message"_q, QString::number(_reply.messageId.msg.bare) },
		{ u"topic"_q, QString::number(_reply.topicRootId.bare) },
		{ u"sublist"_q, QString::number(_reply.monoforumPeerId.value) },
		{ u"text"_q, _draftText.text },
		{ u"tags"_q, tags },
		{ u"quote"_q, _reply.quote.text },
		{ u"quoteTags"_q, quoteTags },
		{ u"quoteOffset"_q, _reply.quoteOffset },
	}).toJson(QJsonDocument::Compact);
	WriteFolder(&session(), _folder, settings, false);
	_settings.draft = settings.draft;
}

void Widget::restoreDraft() {
	const auto object = QJsonDocument::fromJson(_settings.draft).object();
	_draftText.text = object.value(u"text"_q).toString();
	for (const auto &entry : object.value(u"tags"_q).toArray()) {
		const auto tag = entry.toObject();
		const auto offset = tag.value(u"offset"_q).toInt();
		const auto length = tag.value(u"length"_q).toInt();
		if (offset >= 0 && offset <= _draftText.text.size()
			&& length > 0 && length <= _draftText.text.size() - offset) {
			_draftText.tags.push_back({ offset, length, tag.value(u"id"_q).toString() });
		}
	}
	auto reply = FullReplyTo{
		.messageId = FullId({ object.value(u"peer"_q).toString().toULongLong(), object.value(u"message"_q).toString().toLongLong() }),
		.quote = { object.value(u"quote"_q).toString() },
		.topicRootId = MsgId(object.value(u"topic"_q).toString().toLongLong()),
		.monoforumPeerId = PeerId(object.value(u"sublist"_q).toString().toULongLong()),
		.quoteOffset = std::max(0, object.value(u"quoteOffset"_q).toInt()),
	};
	auto quoteTags = TextWithTags::Tags();
	for (const auto &entry : object.value(u"quoteTags"_q).toArray()) {
		const auto tag = entry.toObject();
		const auto offset = tag.value(u"offset"_q).toInt();
		const auto length = tag.value(u"length"_q).toInt();
		if (offset >= 0 && offset <= reply.quote.text.size()
			&& length > 0 && length <= reply.quote.text.size() - offset) {
			quoteTags.push_back({ offset, length, tag.value(u"id"_q).toString() });
		}
	}
	reply.quote.entities = TextUtilities::ConvertTextTagsToEntities(quoteTags);
	if (reply.messageId) {
		const auto revision = _replyRevision;
		session().api().requestMessageData(session().data().peer(reply.messageId.peer), reply.messageId.msg, crl::guard(this, [=] {
			if (revision == _replyRevision && ValidReply(&session(), reply)) {
				chooseReply(reply);
			}
		}));
	}
}

void Widget::clearReply() {
	saveDraft();
	_reply = {};
	++_replyRevision;
	_composeLifetime.destroy();
	_compose = nullptr;
	_replyHint->setText(tr::ayu_AggregateChooseReply(tr::now));
	_replyHint->setClickedCallback(nullptr);
	saveDraft();
	updateGeometry();
}

void Widget::chooseReply(FullReplyTo reply) {
	if (const auto item = session().data().message(reply.messageId)) {
		reply.topicRootId = item->history()->peer->isForum() ? item->topicRootId() : MsgId();
		reply.monoforumPeerId = item->history()->peer->isMonoforum() ? item->sublistPeerId() : PeerId();
	}
	if (!ValidReply(&session(), reply)) {
		controller()->showToast(tr::ayu_AggregateInvalidReply(tr::now));
		return;
	}
	if (_compose) {
		_draftText = _compose->fieldForMention()->getTextWithTags();
	}
	_composeLifetime.destroy();
	_compose = nullptr;
	_reply = reply;
	++_replyRevision;
	const auto item = session().data().message(reply.messageId);
	const auto history = item->history();
	_compose = std::make_unique<ComposeControls>(this, ComposeControlsDescriptor{
		.show = controller()->uiShow(),
		.sendMenuDetails = [] { return SendMenu::Details(); },
		.regularWindow = controller(),
		.stickerOrEmojiChosen = controller()->stickerOrEmojiChosen(),
		.externalDraft = true,
		.features = {
			.sendAs = false,
			.ttlInfo = false,
			.botCommandSend = false,
			.silentBroadcastToggle = false,
			.attachBotsMenu = false,
			.inlineBots = false,
			.autocompleteCommands = false,
			.commonTabbedPanel = false,
			.richEditor = false,
		},
	});
	_compose->setHistory({
		.history = history,
		.topicRootId = reply.topicRootId,
		.monoforumPeerId = reply.monoforumPeerId,
		.showSlowmodeError = [] { return false; },
		.showScheduleSendError = [] { return true; },
		.slowmodeSecondsLeft = rpl::single(0),
		.sendDisabledBySlowmode = rpl::single(false),
		.writeRestriction = rpl::single(Controls::WriteRestriction()),
		.canSendTexts = Data::CanSendTextsValue(history->peer),
	});
	_compose->replyToMessage(reply);
	_compose->setText(_draftText);
	_replyHint->setText(history->peer->name() + u" · "_q + tr::ayu_AggregateOpenOriginal(tr::now));
	_replyHint->setClickedCallback([=] { open(reply.messageId); });
	_compose->height() | rpl::on_next([=] { updateGeometry(); }, _composeLifetime);
	_compose->fieldForMention()->changes() | rpl::on_next([=] {
		++_replyRevision;
		saveDraft();
	}, _composeLifetime);
	_compose->replyCancelled() | rpl::on_next([=] {
		const auto revision = _replyRevision;
		crl::on_main(this, [=] {
			if (revision == _replyRevision) {
				clearReply();
			}
		});
	}, _composeLifetime);
	_compose->cancelRequests() | rpl::on_next([=] {
		const auto revision = _replyRevision;
		crl::on_main(this, [=] {
			if (revision == _replyRevision) {
				clearReply();
			}
		});
	}, _composeLifetime);
	_compose->sendRequests() | rpl::on_next([=](Api::SendOptions options) {
		const auto text = _compose->getTextWithAppliedMarkdown();
		if (text.text.trimmed().isEmpty()) {
			return;
		}
		const auto state = std::make_shared<TextSendState>();
		state->webPage = _compose->webPageDraft();
		auto left = TextWithEntities{ text.text, TextUtilities::ConvertTextTagsToEntities(text.tags) };
		auto part = tr::marked();
		const auto limit = Data::PremiumLimits(&session()).messageLengthCurrent();
		while (TextUtilities::CutPart(part, left, limit)) {
			state->parts.push_back({ part.text, TextUtilities::ConvertEntitiesToTextTags(part.entities) });
		}
		submit(reply, options, int(state->parts.size()), [=](Api::SendAction action, Api::SendCompletionPtr completion) {
			completion->addRequest();
			completion->seal();
			SendNextText(state, action, completion);
		});
	}, _composeLifetime);
	_compose->sendVoiceRequests() | rpl::on_next([=](const ComposeControls::VoiceToSend &voice) {
		submit(reply, voice.options, 1, [=](Api::SendAction action, Api::SendCompletionPtr completion) {
			action.history->session().api().sendVoiceMessage(voice.bytes, voice.waveform, voice.duration, voice.video, action, completion);
		});
	}, _composeLifetime);
	_compose->fileChosen() | rpl::on_next([=](ChatHelpers::FileChosen chosen) {
		submit(reply, chosen.options, 1, [=](Api::SendAction action, Api::SendCompletionPtr completion) {
			auto message = Api::MessageToSend(action);
			message.textWithTags = chosen.caption;
			message.completion = completion;
			Api::SendExistingDocument(std::move(message), chosen.document);
		});
	}, _composeLifetime);
	_compose->photoChosen() | rpl::on_next([=](ChatHelpers::PhotoChosen chosen) {
		submit(reply, chosen.options, 1, [=](Api::SendAction action, Api::SendCompletionPtr completion) {
			auto message = Api::MessageToSend(action);
			message.completion = completion;
			Api::SendExistingPhoto(std::move(message), chosen.photo);
		});
	}, _composeLifetime);
	_compose->attachRequests() | rpl::on_next([=](std::optional<bool> compress) { attach(compress); }, _composeLifetime);
	_compose->setMimeDataHook([=](not_null<const QMimeData*> mime, Ui::InputField::MimeAction action) {
		if (!Core::CanSendFiles(mime)) {
			return false;
		} else if (action == Ui::InputField::MimeAction::Check) {
			return true;
		}
		if (const auto urls = Core::ReadMimeUrls(mime); !urls.empty()) {
			auto list = Storage::PrepareMediaList(urls, st::sendMediaPreviewSize, session().premium());
			if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
				confirmFiles(std::move(list), reply);
				return true;
			}
		}
		if (auto image = Core::ReadMimeImage(mime)) {
			confirmFiles(Storage::PrepareMediaFromImage(
				std::move(image.image), std::move(image.content), st::sendMediaPreviewSize), reply);
			return true;
		}
		return false;
	});
	_compose->setSendAsFileConfirmed(crl::guard(this, [=](std::shared_ptr<Ui::PreparedBundle> bundle, Api::SendOptions options) {
		sendFiles(std::move(bundle), options, reply);
	}));
	_compose->show();
	_compose->finishAnimating();
	_compose->focus();
	saveDraft();
	updateGeometry();
}

void Widget::submit(FullReplyTo reply, Api::SendOptions options, int count, Fn<void(Api::SendAction, Api::SendCompletionPtr)> operation, std::optional<QByteArray> submittedDraft, int sendingCount) {
	if (_job->sending) {
		return;
	}
	if (!ReadGlobal().enabled || !ValidReply(&session(), reply)) {
		controller()->showToast(tr::ayu_AggregateInvalidReply(tr::now));
		return;
	}
	const auto history = session().data().history(reply.messageId.peer);
	const auto thread = history->threadFor(reply.topicRootId, reply.monoforumPeerId);
	if (const auto error = GetErrorForSending(thread ? thread : static_cast<Data::Thread*>(history), { .topicRootId = reply.topicRootId, .messagesCount = sendingCount ? sendingCount : count })) {
		Data::ShowSendErrorToast(controller(), history->peer, error);
		return;
	}
	if (options.scheduled || options.shortcutId || options.suggest) {
		open(reply.messageId);
		return;
	}
	if (!_payment.check(controller(), history->peer, options, count, crl::guard(this, [=](int approved) {
		auto paid = options;
		paid.starsApproved = approved;
		submit(reply, paid, count, operation, submittedDraft, sendingCount);
	}))) {
		return;
	}
	saveDraft();
	const auto draft = submittedDraft.value_or((_reply == reply) ? _settings.draft : QByteArray());
	const auto account = &session();
	const auto folder = _folder;
	const auto job = _job;
	job->reply = reply;
	job->options = options;
	job->count = count;
	job->sendingCount = sendingCount;
	job->draft = draft;
	job->operation = operation;
	job->sending = true;
	job->failed = false;
	_retry = nullptr;
	auto action = Api::SendAction(history, options);
	action.replyTo = reply;
	action.options.requiredReply = reply;
	action.clearDraft = false;
	operation(action, Api::MakeSendCompletion([=](bool success) {
		if (success) {
			ConfirmReplyRead(account, reply);
			auto settings = ReadFolder(account, folder);
			if (settings.draft == draft) {
				settings.draft.clear();
				WriteFolder(account, folder, settings, false);
			}
		}
		job->sending = false;
		job->failed = !success;
		if (success) {
			job->operation = nullptr;
		}
		job->completed.fire_copy(success);
	}));
}

void Widget::attach(std::optional<bool> compress) {
	saveDraft();
	const auto reply = _reply;
	const auto draft = _settings.draft;
	const auto caption = _compose->getTextWithAppliedMarkdown();
	FileDialog::GetOpenPaths(this, tr::lng_choose_files(tr::now), FileDialog::AllOrImagesFilter(), crl::guard(this, [=](FileDialog::OpenResult &&result) {
		auto list = Storage::PreparedFileFromFilesDialog(std::move(result), [](const Ui::PreparedList &list) {
			return list.error == Ui::PreparedList::Error::None;
		}, [=](tr::phrase<> error) {
			controller()->showToast(error(tr::now));
		}, st::sendMediaPreviewSize, session().premium());
		if (list) {
			list->overrideSendImagesAsPhotos = compress;
			confirmFiles(std::move(*list), reply, caption, draft);
		}
	}), nullptr);
}

void Widget::confirmFiles(Ui::PreparedList &&list, FullReplyTo reply, std::optional<TextWithTags> caption, std::optional<QByteArray> submittedDraft) {
	if (!ValidReply(&session(), reply) || !_compose) {
		return;
	}
	if (Data::ShowSendError(controller()->uiShow(), session().data().peer(reply.messageId.peer), list, std::nullopt, false, false)) {
		return;
	}
	saveDraft();
	const auto draft = submittedDraft.value_or((_reply == reply) ? _settings.draft : QByteArray());
	auto box = Box<SendFilesBox>(controller(), std::move(list), caption.value_or(_compose->getTextWithAppliedMarkdown()), session().data().peer(reply.messageId.peer), Api::SendType::Normal, SendMenu::Details());
	box->setReplyTo(reply, true);
	box->setConfirmedCallback(crl::guard(this, [=](std::shared_ptr<Ui::PreparedBundle> bundle, Api::SendOptions options, FullReplyTo currentReply) {
		sendFiles(std::move(bundle), options, reply, draft);
	}));
	controller()->show(std::move(box));
}

[[nodiscard]] Ui::PreparedFile CopyFile(const Ui::PreparedFile &file) {
	auto copy = Ui::PreparedFile(file.path);
	copy.displayName = file.displayName;
	copy.caption = file.caption;
	copy.content = file.content;
	copy.size = file.size;
	copy.information = file.information ? std::make_unique<Ui::PreparedFileInformation>(*file.information) : nullptr;
	copy.videoCover = file.videoCover ? std::make_unique<Ui::PreparedFile>(CopyFile(*file.videoCover)) : nullptr;
	copy.preview = file.preview;
	copy.shownDimensions = file.shownDimensions;
	copy.originalDimensions = file.originalDimensions;
	copy.type = file.type;
	copy.ttlSeconds = file.ttlSeconds;
	copy.spoiler = file.spoiler;
	copy.sendLargePhotos = file.sendLargePhotos;
	copy.animationJob = file.animationJob;
	copy.archive = file.archive;
	return copy;
}

struct FileSendState {
	std::shared_ptr<Ui::PreparedBundle> bundle;
	int group = 0;
	int file = 0;
};

void SendNextFile(
		std::shared_ptr<FileSendState> state,
		Api::SendAction action,
		Api::SendCompletionPtr completion) {
	if (state->group == state->bundle->groups.size()) {
		completion->finishRequest(true);
		return;
	}
	const auto &group = state->bundle->groups[state->group];
	const auto album = group.type != Ui::AlbumType::None;
	auto list = Ui::PreparedList();
	if (album) {
		for (const auto &file : group.list.files) {
			list.files.push_back(CopyFile(file));
		}
	} else {
		list.files.push_back(CopyFile(group.list.files[state->file]));
	}
	action.history->session().api().sendFiles(
		std::move(list),
		state->bundle->way.sendImagesAsPhotos() ? SendMediaType::Photo : SendMediaType::File,
		album ? std::make_shared<SendingAlbum>() : nullptr,
		action,
		Api::MakeSendCompletion([=](bool success) {
			if (!success) {
				completion->finishRequest(false);
				return;
			}
			if (album || ++state->file == state->bundle->groups[state->group].list.files.size()) {
				++state->group;
				state->file = 0;
			}
			SendNextFile(state, action, completion);
		}));
}

void Widget::sendFiles(std::shared_ptr<Ui::PreparedBundle> bundle, Api::SendOptions options, FullReplyTo reply, std::optional<QByteArray> submittedDraft) {
	if (!bundle || !bundle->totalCount) {
		return;
	}
	const auto state = std::make_shared<FileSendState>();
	state->bundle = bundle;
	auto sendingCount = 0;
	for (const auto &group : bundle->groups) {
		sendingCount += (group.type != Ui::AlbumType::None)
			? 1 : int(group.list.files.size());
	}
	submit(reply, options, bundle->totalCount, [=](Api::SendAction action, Api::SendCompletionPtr completion) {
		completion->addRequest();
		completion->seal();
		SendNextFile(state, action, completion);
	}, submittedDraft, sendingCount);
}

void Widget::filters(bool chats) {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::aggregateFilterPopup);
	const auto add = [&](QString label, const style::icon &icon, Mask bit, bool sources) {
		auto action = new QAction(label, _menu.get());
		action->setCheckable(true);
		const auto mask = sources ? _settings.sources : _settings.categories;
		action->setChecked(bit ? bool(mask & bit) : !mask);
		auto item = base::make_unique_q<FilterAction>(_menu->menu(), action, icon);
		item->setPreventClose(true);
		QObject::connect(action, &QAction::triggered, this, [=] {
			_settings = ReadFolder(&session(), _folder);
			auto &mask = sources ? _settings.sources : _settings.categories;
			mask = bit ? Toggle(mask, bit) : 0;
			WriteFolder(&session(), _folder, _settings, false);
			_data->setFilters(_settings.categories, _settings.sources, _settings.newestFirst);
			for (const auto entry : _menu->actions()) {
				if (entry->isSeparator() || entry->property("muteFilter").toBool()
					|| entry->property("sources").toBool() != sources) {
					continue;
				}
				const auto flag = Mask(entry->property("mask").toUInt());
				entry->setChecked(flag ? bool(mask & flag) : !mask);
			}
		});
		action->setProperty("sources", sources);
		action->setProperty("mask", uint(bit));
		_menu->addAction(std::move(item));
	};
	if (chats) {
		add(tr::lng_filters_all(tr::now), st::menuIconShowAll, 0, true);
		for (auto i = 0; i != 4; ++i) {
			add(SourceLabel(SourceType(i)), SourceIcon(SourceType(i)), Bit(SourceType(i)), true);
		}
		_menu->addSeparator();
		for (const auto unmuted : { false, true }) {
			const auto label = unmuted ? tr::lng_settings_unmuted_chats(tr::now) : tr::lng_filters_all(tr::now);
			const auto action = new QAction(label, _menu.get());
			action->setCheckable(true);
			action->setChecked(_settings.unmutedOnly == unmuted);
			action->setProperty("muteFilter", true);
			action->setProperty("unmuted", unmuted);
			auto item = base::make_unique_q<FilterAction>(_menu->menu(), action,
				unmuted ? st::menuIconUnmute : st::menuIconShowAll);
			item->setPreventClose(true);
			QObject::connect(action, &QAction::triggered, this, [=] {
				_settings = ReadFolder(&session(), _folder);
				_settings.unmutedOnly = unmuted;
				WriteFolder(&session(), _folder, _settings, false);
				_data->setFilters(_settings.categories, _settings.sources, _settings.newestFirst);
				for (const auto entry : _menu->actions()) {
					if (entry->property("muteFilter").toBool()) {
						entry->setChecked(entry->property("unmuted").toBool() == unmuted);
					}
				}
			});
			_menu->addAction(std::move(item));
		}
	} else {
		add(tr::lng_filters_all_short(tr::now), st::menuIconShowAll, 0, false);
		for (auto i = 0; i != int(Category::Count); ++i) {
			add(CategoryLabel(Category(i)), CategoryIcon(Category(i)), Bit(Category(i)), false);
		}
	}
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	const auto button = chats ? _chatFilter.data() : _filter.data();
	_menu->popup(button->mapToGlobal(QPoint(button->width(), button->height())));
}

void Widget::menu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	if (_mediaData) {
		_menu->addAction(tr::ayu_AggregateTitle(tr::now), [=] { closeMedia(); });
	}
	_menu->addAction(tr::ayu_AggregateRefresh(tr::now), [=] { refresh(); });
	_menu->addAction(_settings.newestFirst ? tr::ayu_AggregateNewestBottom(tr::now) : tr::ayu_AggregateNewestTop(tr::now), [=] {
		_settings = ReadFolder(&session(), _folder);
		_settings.newestFirst = !_settings.newestFirst;
		WriteFolder(&session(), _folder, _settings, false);
		_data->setFilters(_settings.categories, _settings.sources, _settings.newestFirst);
	});
	_menu->addAction(tr::ayu_AggregateSources(tr::now), [=] { sources(); });
	_menu->addAction(tr::lng_menu_settings(tr::now), [=] { controller()->show(Box(SettingsBox)); }, &st::menuIconSettings);
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(_more->mapToGlobal(QPoint(_more->width(), _more->height())));
}

void Widget::sources() {
	if (_sidebar) {
		if (const auto sidebar = dynamic_cast<SidebarWidget*>(_sidebar.data())) {
			_sidebarTop = sidebar->scrollTop();
			_sidebarLimit = sidebar->sourceLimit();
		}
		_sidebarOpen = false;
		controller()->closeThirdSection();
		return;
	}
	ensureSidebar();
}

void Widget::ensureSidebar() {
	_sidebarOpen = true;
	if (_sidebar) return;
	const auto memento = std::make_shared<SidebarMemento>(
		_folder, _data, crl::guard(this, [=](Category category) { showMedia(category); }),
		crl::guard(this, [=](Window::SectionWidget *widget) {
			_sidebar = widget;
			_info->setIconOverride(&st::topBarInfoActive);
			QObject::connect(widget, &QObject::destroyed, this, [=] {
				_info->setIconOverride(nullptr);
			});
		}));
	memento->closed = crl::guard(this, [=] { _sidebarOpen = false; });
	memento->owner = std::static_pointer_cast<Memento>(createMemento());
	memento->owner->sidebarOpen = true;
	memento->owner->sidebarTop = _sidebarTop;
	memento->owner->sidebarLimit = _sidebarLimit;
	if (controller()->canShowThirdSection()) {
		Core::App().settings().setThirdSectionInfoEnabled(true);
		Core::App().saveSettingsDelayed();
		controller()->resizeForThirdSection();
		controller()->updateColumnLayout();
	}
	controller()->showSection(memento, Window::SectionShow().withThirdColumn());
}

void Widget::closeMedia() {
	_mediaLifetime.destroy();
	_mediaScroll = nullptr;
	_mediaList = nullptr;
	_mediaController = nullptr;
	_mediaData = nullptr;
	_title->update();
	updateStatus();
	if (_scroll) {
		_scroll->show();
	}
}

void Widget::showMedia(Category category) {
	if (category == Category::Count) {
		closeMedia();
		return;
	}
	using Type = Storage::SharedMediaType;
	const auto type = category == Category::Photo ? Type::Photo
		: category == Category::Video ? Type::Video
		: category == Category::Music ? Type::MusicFile
		: category == Category::Voice ? Type::RoundVoiceFile
		: category == Category::Link ? Type::Link
		: category == Category::Gif ? Type::GIF : Type::File;
	closeMedia();
	_mediaCategory = category;
	_mediaData = _data;
	_mediaController = std::make_unique<MediaController>(controller(), type);
	auto source = [=](Data::MessagePosition around, int limit) {
		return rpl::single(rpl::empty_value())
			| rpl::then(_mediaData->changed())
			| rpl::map([=] { return _mediaData->mediaSlice(category, around, limit); });
	};
	_mediaScroll = std::make_unique<Ui::ElasticScroll>(this);
	_paging.reset();
	base::install_event_filter(this, _mediaScroll.get(), [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Wheel || event->type() == QEvent::KeyPress
			|| event->type() == QEvent::TouchUpdate || event->type() == QEvent::MouseButtonPress) _paging.reset();
		return base::EventFilterResult::Continue;
	});
	_mediaScroll->setBottomContentRequest([=] {
		requestMore();
		return false;
	});
	_mediaList = _mediaScroll->setOwnedWidget(object_ptr<Info::Media::ListWidget>(this, _mediaController.get(),
		std::make_unique<Info::GlobalMedia::Provider>(_mediaController.get(), std::move(source),
			[=](FullMsgId id) { return mediaNavigation(id, false); })));
	_mediaList->setGridScale(ReadGlobal().mediaScale);
	SettingsChanged() | rpl::on_next([=] {
		_mediaList->setGridScale(ReadGlobal().mediaScale);
	}, _mediaLifetime);
	_mediaData->changed() | rpl::on_next([=] {
		updateStatus();
	}, _mediaLifetime);
	_mediaScroll->scrolls() | rpl::on_next([=] { onScroll(); }, _mediaLifetime);
	_mediaList->scrollToRequests() | rpl::on_next([=](int top) { _mediaScroll->scrollToY(top); }, _mediaLifetime);
	_title->update();
	_scroll->hide();
	_mediaScroll->show();
	updateGeometry();
}

} // namespace

Memento::Memento(FilterId folder, std::shared_ptr<Controller> data)
: folder(folder)
, data(std::move(data))
, list(Data::MaxMessagePosition) {
}

object_ptr<Window::SectionWidget> Memento::createWidget(QWidget *parent, not_null<Window::SessionController*> controller, Window::Column column, const QRect &geometry) {
	if (column == Window::Column::Third || !ReadGlobal().enabled) {
		return nullptr;
	}
	auto result = object_ptr<Widget>(parent, controller, this);
	result->setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(result);
	return result;
}

object_ptr<Ui::AbstractButton> CreateEntry(
		QWidget *parent, not_null<Window::SessionController*> controller) {
	auto result = object_ptr<Ui::RippleButton>(parent, st::dialogsRipple);
	const auto button = result.data();
	button->resize(button->width(), st::defaultDialogRow.height);
	button->setAccessibleName(tr::ayu_AggregateTitle(tr::now));
	button->paintRequest() | rpl::on_next([=] {
		Painter p(button);
		const auto &row = st::defaultDialogRow;
		const auto active = controller->activeAggregateCurrent() == std::optional<int>(controller->activeChatsFilterCurrent());
		p.fillRect(button->rect(), active ? st::dialogsBgActive : button->isOver() ? st::dialogsBgOver : st::dialogsBg);
		button->paintRipple(p, 0, 0);
		PaintFolder(p, QRect(row.padding.left(), row.padding.top(), row.photoSize, row.photoSize), button->width(),
			&controller->session(), controller->activeChatsFilterCurrent());
		p.setFont(st::semiboldFont);
		p.setPen(active ? st::dialogsNameFgActive : st::dialogsNameFg);
		p.drawTextLeft(row.nameLeft, row.nameTop, button->width(),
			st::semiboldFont->elided(tr::ayu_AggregateTitle(tr::now), std::max(0, button->width() - row.nameLeft - row.padding.right())));
		auto title = tr::lng_filters_all(tr::now);
		for (const auto &filter : controller->session().data().chatsFilters().list()) {
			if (filter.id() == controller->activeChatsFilterCurrent()) {
				title = filter.titleText().text;
				break;
			}
		}
		p.setFont(st::dialogsTextFont);
		p.setPen(active ? st::dialogsTextFgActive : st::dialogsTextFg);
		p.drawTextLeft(row.textLeft, row.textTop, button->width(),
			st::dialogsTextFont->elided(title, std::max(0, button->width() - row.textLeft - row.padding.right())));
	}, button->lifetime());
	controller->activeAggregateValue() | rpl::on_next([=] { button->update(); }, button->lifetime());
	controller->activeChatsFilter() | rpl::on_next([=] { button->update(); }, button->lifetime());
	controller->session().data().chatsFilters().changed()
	| rpl::on_next([=] { button->update(); }, button->lifetime());
	return result;
}

void Show(not_null<Window::SessionController*> controller, FilterId folder) {
	if (ReadGlobal().enabled) {
		auto &cache = ReadingCache(&controller->session());
		const auto i = cache.find(folder);
		const auto memento = (i != cache.end())
			? std::make_shared<Memento>(*i->second)
			: std::make_shared<Memento>(folder);
		memento->data = ControllerFor(&controller->session(), folder);
		memento->intent = EntryIntent(controller->activeAggregateCurrent() == std::optional<int>(folder));
		controller->showSection(memento);
	}
}

} // namespace Ayu::Aggregate
