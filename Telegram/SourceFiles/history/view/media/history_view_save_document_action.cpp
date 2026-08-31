/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_save_document_action.h"

#include "base/call_delayed.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_download_manager.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "boxes/abstract_box.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtWidgets/QApplication>

namespace HistoryView {
namespace {

QString LivePhotoVideoFileNameForSave(
		not_null<DocumentData*> document,
		const QString &current) {
	const auto info = QFileInfo(current);
	const auto mime = Core::MimeTypeForName(document->mimeString());
	const auto patterns = mime.globPatterns();
	const auto pattern = patterns.isEmpty() ? QString() : patterns.front();
	auto name = document->filename();
	if (name.isEmpty() && !current.isEmpty()) {
		name = info.fileName();
	}
	if (name.isEmpty()) {
		name = pattern.isEmpty()
			? u".mov"_q
			: QString(pattern).replace('*', QString());
	}
	const auto filter = pattern.isEmpty()
		? (u"MOV Video (*.mov);;"_q + FileDialog::AllFilesFilter())
		: (mime.filterString() + u";;"_q + FileDialog::AllFilesFilter());
	return FileNameForSave(
		&document->session(),
		tr::lng_save_video(tr::now),
		filter,
		u"video"_q,
		name,
		true,
		current.isEmpty() ? QDir() : info.dir());
}

void ShowLivePhotoVideoSaveFailed() {
	Ui::show(Ui::MakeInformBox(tr::lng_download_finish_failed()));
}

void TrackLoadedLivePhotoVideo(
		FullMsgId itemId,
		not_null<DocumentData*> document,
		const QString &path) {
	if (const auto item = document->owner().message(itemId)) {
		auto &manager = Core::App().downloadManager();
		manager.addLoaded({
			.item = item,
			.document = document,
		}, path, manager.computeNextStartDate());
	}
}

void TrackLoadingLivePhotoVideo(
		FullMsgId itemId,
		not_null<DocumentData*> document) {
	if (const auto item = document->owner().message(itemId)) {
		Core::App().downloadManager().addLoading({
			.item = item,
			.document = document,
			.forceShowInManager = true,
		});
	}
}

} // namespace

void AddSaveDocumentAction(
		const Ui::Menu::MenuCallback &addAction,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document,
		not_null<Window::SessionController*> controller) {
	const auto contextId = item->fullId();
	const auto fromSaved = item->history()->peer->isSelf();
	const auto savedMusic = &document->owner().savedMusic();
	const auto show = controller->uiShow();
	const auto inProfile = savedMusic->has(document);
	const auto &ripple = st::defaultDropdownMenu.menu.ripple;
	const auto duration = ripple.hideDuration;
	const auto saveAs = base::fn_delayed(duration, controller, [=] {
		DocumentSaveClickHandler::SaveAndTrack(
			contextId,
			document,
			DocumentSaveClickHandler::Mode::ToNewFile);
	});
	if (!document->isMusicForProfile() || (fromSaved && inProfile)) {
		const auto text = document->isVideoFile()
			? tr::lng_context_save_video(tr::now)
			: document->isVoiceMessage()
			? tr::lng_context_save_audio(tr::now)
			: document->isAudioFile()
			? tr::lng_context_save_audio_file(tr::now)
			: document->sticker()
			? tr::lng_context_save_image(tr::now)
			: tr::lng_context_save_file(tr::now);
		addAction(text, saveAs, &st::menuIconDownload);
		return;
	}
	const auto fill = [&](not_null<Ui::PopupMenu*> menu) {
		if (!inProfile) {
			const auto saved = [=] {
				savedMusic->save(document, contextId);
				show->showToast({
					.text = { tr::lng_saved_music_added(tr::now) },
					.iconLottie = u"toast/save_to_music"_q,
					.iconLottieSize = st::toastLottieIconSize,
				});
			};
			menu->addAction(
				tr::lng_context_save_music_profile(tr::now),
				saved,
				&st::menuIconProfile);
		}
		if (!fromSaved) {
			menu->addAction(
				tr::lng_context_save_music_saved(tr::now),
				[=] { Window::ForwardToSelf(show, { { contextId } }); },
				&st::menuIconSavedMessages);
		}
		menu->addAction(
			tr::lng_context_save_music_folder(tr::now),
			saveAs,
			&st::menuIconDownload);

		menu->addSeparator(&st::expandedMenuSeparator);

		auto item = base::make_unique_q<Ui::Menu::MultilineAction>(
			menu->menu(),
			st::saveMusicInfoMenu,
			st::historyHasCustomEmoji,
			QPoint(
				st::saveMusicInfoMenu.itemPadding.left(),
				st::saveMusicInfoMenu.itemPadding.top()),
			TextWithEntities{ tr::lng_context_save_music_about(tr::now) });
		item->setAttribute(Qt::WA_TransparentForMouseEvents);

		item->setPointerCursor(false);
		menu->addAction(std::move(item));
	};
	addAction(Ui::Menu::MenuCallback::Args{
		.text = tr::lng_context_save_music_to(tr::now),
		.handler = nullptr,
		.icon = &st::menuIconSoundAdd,
		.fillSubmenu = fill,
		.submenuSt = &st::popupMenuWithIcons,
	});
}

void AddSaveDocumentAction(
		not_null<Ui::PopupMenu*> menu,
		HistoryItem *item,
		not_null<DocumentData*> document,
		not_null<ListWidget*> list) {
	if (!item || list->hasCopyMediaRestriction(item) || ItemHasTtl(item)) {
		return;
	}
	AddSaveDocumentAction(
		Ui::Menu::CreateAddActionCallback(menu),
		item,
		document,
		list->controller());
}

void SaveLivePhotoVideo(
		FullMsgId itemId,
		not_null<DocumentData*> document) {
	if (document->isNull()) {
		return;
	}
	InvokeQueued(qApp, crl::guard(&document->session(), [=] {
		const auto current = document->filepath(true);
		const auto target = LivePhotoVideoFileNameForSave(document, current);
		if (target.isEmpty()) {
			return;
		}

		const auto media = document->createMediaView();
		const auto bytes = media->bytes();
		if (!bytes.isEmpty()) {
			auto output = QSaveFile(target);
			const auto saved = output.open(QIODevice::WriteOnly)
				&& (output.write(bytes) == bytes.size())
				&& output.commit();
			if (!saved) {
				output.cancelWriting();
				ShowLivePhotoVideoSaveFailed();
				return;
			}
			TrackLoadedLivePhotoVideo(itemId, document, target);
			return;
		}

		const auto &location = document->location(true);
		if (location.accessEnable()) {
			const auto source = location.name();
			const auto saved = (source == target) || [&] {
				QFile::remove(target);
				return QFile::copy(source, target);
			}();
			location.accessDisable();
			if (!saved) {
				ShowLivePhotoVideoSaveFailed();
				return;
			}
			TrackLoadedLivePhotoVideo(itemId, document, target);
			return;
		}

		document->save(itemId ? itemId : Data::FileOrigin(), target);
		if (document->loading() && !document->loadingFilePath().isEmpty()) {
			TrackLoadingLivePhotoVideo(itemId, document);
		} else if (QFileInfo::exists(target)) {
			TrackLoadedLivePhotoVideo(itemId, document, target);
		} else {
			ShowLivePhotoVideoSaveFailed();
		}
	}));
}

void AddSaveLivePhotoVideoAction(
		const Ui::Menu::MenuCallback &addAction,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document,
		not_null<Window::SessionController*> controller) {
	const auto contextId = item->fullId();
	const auto duration = st::defaultDropdownMenu.menu.ripple.hideDuration;
	addAction(
		tr::ayu_SaveLivePhotoVideo(tr::now),
		base::fn_delayed(duration, controller, [=] {
			SaveLivePhotoVideo(contextId, document);
		}),
		&st::menuIconDownload);
}

void AddSaveLivePhotoVideoAction(
		not_null<Ui::PopupMenu*> menu,
		HistoryItem *item,
		not_null<DocumentData*> document,
		not_null<Window::SessionController*> controller) {
	if (!item || ItemHasTtl(item)) {
		return;
	}
	AddSaveLivePhotoVideoAction(
		Ui::Menu::CreateAddActionCallback(menu),
		item,
		document,
		controller);
}

} // namespace HistoryView
