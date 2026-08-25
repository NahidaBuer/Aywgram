// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/forward/ayu_sync.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/file_download.h"
#include "storage/storage_account.h"

namespace AyuSync {
namespace {

template <typename Value>
using SharedCompletion = std::shared_ptr<FnMut<void(Value)>>;

template <typename Value>
void Complete(const SharedCompletion<Value> &completion, Value value) {
	if (completion && *completion) {
		auto callback = std::move(*completion);
		callback(std::move(value));
	}
}

QString PhotoFilePath(
		not_null<Main::Session*> session,
		not_null<PhotoData*> photo) {
	return pathForSave(session)
		+ QString::number(photo->getDC())
		+ u"_"_q
		+ QString::number(photo->id)
		+ u".jpg"_q;
}

QString DocumentFilePath(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document) {
	const auto path = document->filepath(true);
	if (!path.isEmpty()) {
		return path;
	}
	const auto prefix = pathForSave(session);
	if (!document->filename().isEmpty()) {
		return prefix + document->filename();
	} else if (document->isVoiceMessage()) {
		return prefix
			+ u"audio_"_q
			+ QString::number(document->getDC())
			+ u"_"_q
			+ QString::number(document->id)
			+ u".ogg"_q;
	} else if (document->isVideoMessage()) {
		return prefix
			+ u"round_"_q
			+ QString::number(document->getDC())
			+ u"_"_q
			+ QString::number(document->id)
			+ u".mp4"_q;
	} else if (document->isGifv()) {
		return prefix
			+ u"gif_"_q
			+ QString::number(document->getDC())
			+ u"_"_q
			+ QString::number(document->id)
			+ u".gif"_q;
	} else if (document->isVideoFile()) {
		return prefix
			+ u"video_"_q
			+ QString::number(document->getDC())
			+ u"_"_q
			+ QString::number(document->id)
			+ u".mp4"_q;
	}
	return prefix
		+ u"document_"_q
		+ QString::number(document->getDC())
		+ u"_"_q
		+ QString::number(document->id);
}

QString FilePath(
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return {};
	}
	if (const auto document = media->document()) {
		return DocumentFilePath(session, document);
	} else if (const auto photo = media->photo()) {
		return PhotoFilePath(session, photo);
	}
	return {};
}

bool FileExistsNonEmpty(const QString &path) {
	const auto info = QFileInfo(path);
	return info.isFile() && info.size() > 0;
}

bool FileHasExactSize(const QString &path, int64 expected) {
	const auto info = QFileInfo(path);
	return info.isFile()
		&& info.size() > 0
		&& (!expected || info.size() == expected);
}

std::optional<bool> CheckDocument(
		not_null<DocumentData*> document,
		const QString &path) {
	if (document->status == FileDownloadFailed) {
		return false;
	}
	return FileHasExactSize(path, document->size)
		? std::optional<bool>(true)
		: std::nullopt;
}

std::optional<bool> CheckDocument(
		not_null<Main::Session*> session,
		FullMsgId itemId) {
	const auto item = session->data().message(itemId);
	const auto media = item ? item->media() : nullptr;
	const auto document = media ? media->document() : nullptr;
	if (!document) {
		return false;
	}
	return CheckDocument(document, FilePath(session, item));
}

std::optional<bool> CheckPhoto(
		not_null<PhotoData*> photo,
		const QString &path) {
	if (FileHasExactSize(
			path,
			photo->imageByteSize(Data::PhotoSize::Large))
		|| FileExistsNonEmpty(path)) {
		return true;
	} else if (photo->failed(Data::PhotoSize::Large)) {
		return false;
	}
	const auto view = photo->createMediaView();
	if (view && view->loaded()) {
		if (!view->saveToFile(path)) {
			return false;
		}
		return FileExistsNonEmpty(path);
	}
	return std::nullopt;
}

std::optional<bool> CheckPhoto(
		not_null<Main::Session*> session,
		FullMsgId itemId) {
	const auto item = session->data().message(itemId);
	const auto media = item ? item->media() : nullptr;
	const auto photo = media ? media->photo() : nullptr;
	if (!photo) {
		return false;
	}
	return CheckPhoto(photo, FilePath(session, item));
}

} // namespace

QString pathForSave(not_null<Main::Session*> session) {
	auto path = Core::App().settings().downloadPath();
	if (path.isEmpty()) {
		path = File::DefaultDownloadPath(session);
	} else if (path == FileDialog::Tmp()) {
		path = session->local().tempDirectory();
	}
	if (!path.isEmpty() && !path.endsWith('/')) {
		path += '/';
	}
	QDir().mkpath(path);
	return path;
}

QString filePath(
		not_null<Main::Session*> session,
		FullMsgId itemId) {
	const auto item = session->data().message(itemId);
	return item ? FilePath(session, item) : QString();
}

QString filePath(
		not_null<Main::Session*> session,
		not_null<PhotoData*> photo) {
	return PhotoFilePath(session, photo);
}

void loadPhoto(
		not_null<Main::Session*> session,
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		rpl::lifetime &lifetime,
		FnMut<void(QString)> completion) {
	const auto path = PhotoFilePath(session, photo);
	const auto shared = std::make_shared<FnMut<void(QString)>>(
		std::move(completion));
	if (path.isEmpty()) {
		Complete(shared, QString());
		return;
	}
	if (const auto result = CheckPhoto(photo, path)) {
		Complete(shared, *result ? path : QString());
		return;
	}
	photo->load(Data::PhotoSize::Large, origin);
	const auto weak = base::make_weak(session.get());
	session->downloaderTaskFinished(
	) | rpl::on_next([weak, photo, path, shared] {
		if (!weak) {
			Complete(shared, QString());
			return;
		}
		if (const auto result = CheckPhoto(photo, path)) {
			Complete(shared, *result ? path : QString());
		}
	}, lifetime);
	if (const auto result = CheckPhoto(photo, path)) {
		Complete(shared, *result ? path : QString());
	} else if (!photo->loading(Data::PhotoSize::Large)) {
		Complete(shared, QString());
	}
}

void loadDocument(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		rpl::lifetime &lifetime,
		FnMut<void(QString)> completion) {
	const auto path = DocumentFilePath(session, document);
	const auto shared = std::make_shared<FnMut<void(QString)>>(
		std::move(completion));
	if (path.isEmpty()) {
		Complete(shared, QString());
		return;
	}
	if (const auto result = CheckDocument(document, path)) {
		Complete(shared, *result ? path : QString());
		return;
	}
	const auto info = QFileInfo(path);
	if (info.isFile()) {
		QFile::remove(path);
	}
	document->save(origin, path);
	const auto weak = base::make_weak(session.get());
	session->downloaderTaskFinished(
	) | rpl::on_next([weak, document, path, shared] {
		if (!weak) {
			Complete(shared, QString());
			return;
		}
		if (const auto result = CheckDocument(document, path)) {
			Complete(shared, *result ? path : QString());
		}
	}, lifetime);
	if (const auto result = CheckDocument(document, path)) {
		Complete(shared, *result ? path : QString());
	} else if (!document->loading()) {
		Complete(shared, QString());
	}
}

void loadMedia(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		rpl::lifetime &lifetime,
		FnMut<void(bool)> completion) {
	const auto item = session->data().message(itemId);
	const auto media = item ? item->media() : nullptr;
	const auto document = media ? media->document() : nullptr;
	const auto photo = media ? media->photo() : nullptr;
	const auto shared = std::make_shared<FnMut<void(bool)>>(
		std::move(completion));
	if (!document && !photo) {
		Complete(shared, false);
		return;
	}
	const auto path = FilePath(session, item);
	if (path.isEmpty()) {
		Complete(shared, false);
		return;
	}
	const auto weak = base::make_weak(session.get());
	if (document) {
		if (const auto result = CheckDocument(session, itemId)) {
			Complete(shared, *result);
			return;
		}
		const auto info = QFileInfo(path);
		if (info.isFile()) {
			QFile::remove(path);
		}
		document->save(Data::FileOriginMessage(itemId), path);
		session->downloaderTaskFinished(
		) | rpl::on_next([weak, itemId, shared] {
			const auto session = weak.get();
			if (!session) {
				Complete(shared, false);
				return;
			}
			if (const auto result = CheckDocument(session, itemId)) {
				Complete(shared, *result);
			}
		}, lifetime);
		if (const auto result = CheckDocument(session, itemId)) {
			Complete(shared, *result);
		} else if (!document->loading()) {
			Complete(shared, false);
		}
	} else {
		if (const auto result = CheckPhoto(session, itemId)) {
			Complete(shared, *result);
			return;
		}
		photo->load(
			Data::PhotoSize::Large,
			Data::FileOriginMessage(itemId));
		session->downloaderTaskFinished(
		) | rpl::on_next([weak, itemId, shared] {
			const auto session = weak.get();
			if (!session) {
				Complete(shared, false);
				return;
			}
			if (const auto result = CheckPhoto(session, itemId)) {
				Complete(shared, *result);
			}
		}, lifetime);
		if (const auto result = CheckPhoto(session, itemId)) {
			Complete(shared, *result);
		} else if (!photo->loading(Data::PhotoSize::Large)) {
			Complete(shared, false);
		}
	}
}

} // namespace AyuSync
