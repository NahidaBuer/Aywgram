// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/forward/ayu_forward_rich.h"

#include "api/api_send_completion.h"
#include "apiwrap.h"
#include "ayu/features/forward/ayu_sync.h"
#include "base/flat_map.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "iv/iv_instance.h"
#include "iv/iv_rich_message_serializer.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "ui/chat/attach/attach_prepare.h"

namespace AyuForward {
namespace {

using Block = Iv::RichPage::Block;
using BlockKind = Iv::RichPage::BlockKind;

struct RichMedia {
	base::flat_map<PhotoId, not_null<PhotoData*>> photos;
	base::flat_map<DocumentId, not_null<DocumentData*>> documents;
};

class PrepareTask final : public Task {
public:
	PrepareTask(
		FileLoadTask::Args &&args,
		Fn<void(std::shared_ptr<FilePrepareResult>)> done)
	: _task(std::move(args))
	, _done(std::move(done)) {
	}

	void process() override {
		_task.process({ .generateGoodThumbnail = false });
	}

	void finish() override {
		_done(_task.peekResult());
	}

private:
	FileLoadTask _task;
	Fn<void(std::shared_ptr<FilePrepareResult>)> _done;
};

[[nodiscard]] PhotoData *ResolvePhoto(
		not_null<Main::Session*> session,
		PhotoId id,
		PhotoData *photo) {
	return photo ? photo : (id ? session->data().photo(id).get() : nullptr);
}

[[nodiscard]] DocumentData *ResolveDocument(
		not_null<Main::Session*> session,
		DocumentId id,
		DocumentData *document) {
	return document
		? document
		: (id ? session->data().document(id).get() : nullptr);
}

[[nodiscard]] bool IsSerializableKind(BlockKind kind) {
	switch (kind) {
	case BlockKind::Unsupported:
	case BlockKind::AuthorDate:
	case BlockKind::Embed:
	case BlockKind::EmbedPost:
	case BlockKind::Channel:
	case BlockKind::RelatedArticles:
		return false;
	default:
		return true;
	}
}

void CollectMedia(
		not_null<Main::Session*> session,
		const std::vector<Block> &blocks,
		RichMedia &media) {
	const auto addPhoto = [&](PhotoId id, PhotoData *photo) {
		if (const auto resolved = ResolvePhoto(session, id, photo)) {
			media.photos.emplace(resolved->id, resolved);
		}
	};
	const auto addDocument = [&](DocumentId id, DocumentData *document) {
		if (const auto resolved = ResolveDocument(session, id, document)) {
			media.documents.emplace(resolved->id, resolved);
		}
	};
	for (const auto &block : blocks) {
		if (!IsSerializableKind(block.kind)) {
			continue;
		}
		switch (block.kind) {
		case BlockKind::Photo:
			addPhoto(block.photoId, block.photo);
			break;
		case BlockKind::Video:
		case BlockKind::Audio:
			addDocument(block.documentId, block.document);
			break;
		case BlockKind::GroupedMedia:
			for (const auto &item : block.mediaItems) {
				if (item.kind == BlockKind::Photo) {
					addPhoto(item.photoId, item.photo);
				} else if (item.kind == BlockKind::Video) {
					addDocument(item.documentId, item.document);
				}
			}
			break;
		default:
			break;
		}
		CollectMedia(session, block.blocks, media);
		for (const auto &item : block.listItems) {
			CollectMedia(session, item.blocks, media);
		}
	}
}

void SanitizeRichText(Iv::RichPage::RichText &text) {
	text.anchorId = QString();
	text.anchorIds.clear();
	const auto autolink = [](const EntityInText &entity) {
		switch (entity.type()) {
		case EntityType::Mention:
		case EntityType::Hashtag:
		case EntityType::BotCommand:
		case EntityType::Cashtag:
		case EntityType::Url:
		case EntityType::Email:
		case EntityType::Phone:
		case EntityType::BankCard:
			return true;
		default:
			return false;
		}
	};
	const auto removed = std::ranges::remove_if(text.text.entities, autolink);
	text.text.entities.erase(removed.begin(), removed.end());
}

void PruneBlocks(
	not_null<Main::Session*> session,
	std::vector<Block> &blocks,
	const RichMedia &remap);

[[nodiscard]] bool PrepareBlock(
		not_null<Main::Session*> session,
		Block &block,
		const RichMedia &remap) {
	if (!IsSerializableKind(block.kind)) {
		return false;
	}
	const auto remapPhoto = [&](PhotoId &id, PhotoData *&photo) {
		const auto resolved = ResolvePhoto(session, id, photo);
		const auto i = resolved
			? remap.photos.find(resolved->id)
			: remap.photos.end();
		if (i == remap.photos.end()) {
			return false;
		}
		photo = i->second;
		id = i->second->id;
		return true;
	};
	const auto remapDocument = [&](DocumentId &id, DocumentData *&document) {
		const auto resolved = ResolveDocument(session, id, document);
		const auto i = resolved
			? remap.documents.find(resolved->id)
			: remap.documents.end();
		if (i == remap.documents.end()) {
			return false;
		}
		document = i->second;
		id = i->second->id;
		return true;
	};
	switch (block.kind) {
	case BlockKind::Photo:
		if (!remapPhoto(block.photoId, block.photo)) {
			return false;
		}
		break;
	case BlockKind::Video:
	case BlockKind::Audio:
		if (!remapDocument(block.documentId, block.document)) {
			return false;
		}
		break;
	case BlockKind::GroupedMedia: {
		auto &items = block.mediaItems;
		for (auto i = items.begin(); i != items.end();) {
			auto kept = false;
			if (i->kind == BlockKind::Photo) {
				kept = remapPhoto(i->photoId, i->photo);
			} else if (i->kind == BlockKind::Video) {
				kept = remapDocument(i->documentId, i->document);
			}
			i = kept ? (i + 1) : items.erase(i);
		}
		if (items.empty()) {
			return false;
		}
	} break;
	case BlockKind::Anchor:
		if (block.anchorId.isEmpty()) {
			return false;
		}
		break;
	case BlockKind::Quote:
		if (block.pullquote && !block.blocks.empty()) {
			return false;
		}
		break;
	case BlockKind::Code:
		if (!block.blocks.empty()) {
			return false;
		}
		break;
	case BlockKind::Map:
		if (block.zoom <= 0) {
			return false;
		}
		break;
	default:
		break;
	}
	SanitizeRichText(block.text);
	SanitizeRichText(block.caption);
	if (block.kind != BlockKind::Anchor) {
		block.anchorId = QString();
	}
	for (auto &row : block.tableRows) {
		for (auto &cell : row.cells) {
			SanitizeRichText(cell.text);
		}
	}
	PruneBlocks(session, block.blocks, remap);
	for (auto &item : block.listItems) {
		SanitizeRichText(item.text);
		item.anchorId = QString();
		PruneBlocks(session, item.blocks, remap);
	}
	return true;
}

void PruneBlocks(
		not_null<Main::Session*> session,
		std::vector<Block> &blocks,
		const RichMedia &remap) {
	auto write = blocks.begin();
	for (auto read = blocks.begin(); read != blocks.end(); ++read) {
		if (PrepareBlock(session, *read, remap)) {
			if (write != read) {
				*write = std::move(*read);
			}
			++write;
		}
	}
	blocks.erase(write, blocks.end());
}

[[nodiscard]] MTPInputMedia UploadedInputMedia(
		const std::shared_ptr<FilePrepareResult> &prepared,
		const Api::RemoteFileInfo &info) {
	if (prepared->type == SendMediaType::Photo) {
		return MTP_inputMediaUploadedPhoto(
			MTP_flags(0),
			info.file,
			MTP_vector<MTPInputDocument>(),
			MTP_int(0),
			MTPInputDocument());
	}
	auto attributes = QVector<MTPDocumentAttribute>();
	prepared->document.match([&](const MTPDdocument &data) {
		attributes = data.vattributes().v;
	}, [](const auto &) {
	});
	if (attributes.isEmpty()) {
		attributes.push_back(MTP_documentAttributeFilename(
			MTP_string(prepared->filename)));
	}
	using Flag = MTPDinputMediaUploadedDocument::Flag;
	auto flags = MTPDinputMediaUploadedDocument::Flags();
	if (prepared->forceFile) {
		flags |= Flag::f_force_file;
	}
	if (info.thumb) {
		flags |= Flag::f_thumb;
	}
	return MTP_inputMediaUploadedDocument(
		MTP_flags(flags),
		info.file,
		info.thumb.value_or(MTPInputFile()),
		MTP_string(prepared->filemime),
		MTP_vector<MTPDocumentAttribute>(std::move(attributes)),
		MTP_vector<MTPInputDocument>(),
		MTPInputPhoto(),
		MTP_int(0),
		MTP_int(0));
}

class Request final
	: public RichForwardRequest
	, public std::enable_shared_from_this<Request> {
public:
	Request(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		const Api::SendAction &action,
		FnMut<void(bool)> completion)
	: _session(base::make_weak(session.get()))
	, _itemId(itemId)
	, _action(action)
	, _completion(std::move(completion)) {
	}

	~Request() override {
		cancel();
	}

	void start() {
		if (_cancelled || _finished) {
			return;
		}
		const auto current = session();
		const auto item = current ? current->data().message(_itemId) : nullptr;
		if (!current || !item || !item->richPage()) {
			complete(false);
			return;
		}
		subscribeToUploader();
		const auto weak = weak_from_this();
		Core::App().iv().resolveRichMessage(
			current,
			item,
			[weak](std::shared_ptr<const Iv::RichPage> page) {
				if (const auto strong = weak.lock()) {
					strong->pageResolved(std::move(page));
				}
			});
	}

	void cancel() override {
		if (_cancelled || _finished) {
			return;
		}
		_cancelled = true;
		_lifetime.destroy();
		_downloadLifetime.reset();
		const auto current = session();
		if (current && _prepareTask) {
			current->api().fileLoader()->cancelTask(_prepareTask);
		}
		_prepareTask = kEmptyTaskId;
		if (current && _uploadId) {
			current->uploader().cancel(_uploadId);
		}
		_uploadId = FullMsgId();
		if (current && _requestId) {
			current->api().request(_requestId).cancel();
		}
		_requestId = 0;
	}

private:
	[[nodiscard]] Main::Session *session() const {
		return _session.get();
	}

	void subscribeToUploader() {
		const auto current = session();
		if (!current) {
			return;
		}
		const auto weak = weak_from_this();
		const auto ready = [weak](const Storage::UploadedMedia &data) {
			if (const auto strong = weak.lock()) {
				strong->uploadFinished(data);
			}
		};
		const auto failed = [weak](FullMsgId id) {
			if (const auto strong = weak.lock()) {
				strong->uploadFailed(id);
			}
		};
		current->uploader().photoReady(
		) | rpl::on_next(ready, _lifetime);
		current->uploader().documentReady(
		) | rpl::on_next(ready, _lifetime);
		current->uploader().photoFailed(
		) | rpl::on_next(failed, _lifetime);
		current->uploader().documentFailed(
		) | rpl::on_next(failed, _lifetime);
	}

	void pageResolved(std::shared_ptr<const Iv::RichPage> page) {
		const auto current = session();
		if (_cancelled || !current || !page) {
			complete(false);
			return;
		}
		_page = *page;
		_page.part = false;
		_page.views = 0;
		if (!current->premium()
			&& Iv::RichPageUsesPremiumFormatting(_page)) {
			complete(false);
			return;
		}
		CollectMedia(current, _page.blocks, _source);
		startNextMedia();
	}

	void startNextMedia() {
		if (_cancelled) {
			return;
		}
		const auto current = session();
		if (!current) {
			complete(false);
			return;
		}
		if (_photoIndex != int(_source.photos.size())) {
			const auto i = _source.photos.begin() + _photoIndex;
			_currentPhotoId = i->first;
			_currentDocumentId = 0;
			_downloadLifetime = std::make_unique<rpl::lifetime>();
			const auto weak = weak_from_this();
			AyuSync::loadPhoto(
				current,
				i->second,
				Data::FileOrigin(_itemId),
				*_downloadLifetime,
				[weak](QString path) {
					if (const auto strong = weak.lock()) {
						strong->mediaLoaded(std::move(path));
					}
				});
			return;
		}
		if (_documentIndex != int(_source.documents.size())) {
			const auto i = _source.documents.begin() + _documentIndex;
			_currentPhotoId = 0;
			_currentDocumentId = i->first;
			_downloadLifetime = std::make_unique<rpl::lifetime>();
			const auto weak = weak_from_this();
			AyuSync::loadDocument(
				current,
				i->second,
				Data::FileOrigin(_itemId),
				*_downloadLifetime,
				[weak](QString path) {
					if (const auto strong = weak.lock()) {
						strong->mediaLoaded(std::move(path));
					}
				});
			return;
		}
		serializeAndSend();
	}

	void mediaLoaded(QString path) {
		_downloadLifetime.reset();
		if (_cancelled) {
			return;
		}
		if (path.isEmpty()) {
			finishCurrentMedia(false);
			return;
		}
		const auto current = session();
		if (!current) {
			complete(false);
			return;
		}
		auto type = SendMediaType::Photo;
		auto forceFile = false;
		auto displayName = QString();
		if (_currentDocumentId) {
			const auto i = _source.documents.find(_currentDocumentId);
			Assert(i != _source.documents.end());
			const auto document = i->second;
			const auto playable = document->isVideoFile()
				|| document->isGifv()
				|| document->isSong()
				|| document->isAudioFile()
				|| document->isVoiceMessage();
			type = SendMediaType::File;
			forceFile = !playable;
			displayName = document->filename();
		}
		const auto weak = weak_from_this();
		auto task = std::make_unique<PrepareTask>(
			FileLoadTask::Args{
				.session = current,
				.filepath = std::move(path),
				.type = type,
				.to = FileLoadTo(
					_action.history->peer->id,
					Api::SendOptions(),
					FullReplyTo(),
					MsgId()),
				.forceFile = forceFile,
				.sendLargePhotos = (type == SendMediaType::Photo),
				.displayName = std::move(displayName),
			},
			[weak](std::shared_ptr<FilePrepareResult> prepared) {
				if (const auto strong = weak.lock()) {
					strong->mediaPrepared(std::move(prepared));
				}
			});
		_prepareTask = task->id();
		current->api().fileLoader()->addTask(std::move(task));
	}

	void mediaPrepared(std::shared_ptr<FilePrepareResult> prepared) {
		_prepareTask = kEmptyTaskId;
		if (_cancelled) {
			return;
		}
		const auto current = session();
		if (!current || !prepared) {
			finishCurrentMedia(false);
			return;
		}
		_prepared = std::move(prepared);
		_uploadId = FullMsgId(
			_action.history->peer->id,
			current->data().nextLocalMessageId());
		current->uploader().upload(_uploadId, _prepared);
	}

	void uploadFinished(const Storage::UploadedMedia &data) {
		if (_cancelled || !_uploadId || data.fullId != _uploadId) {
			return;
		}
		_uploadId = FullMsgId();
		const auto current = session();
		if (!current || !_prepared) {
			finishCurrentMedia(false);
			return;
		}
		const auto weak = weak_from_this();
		_requestId = current->api().request(MTPmessages_UploadMedia(
			MTP_flags(0),
			MTPstring(),
			_action.history->peer->input(),
			UploadedInputMedia(_prepared, data.info)
		)).done([weak](const MTPMessageMedia &media) {
			if (const auto strong = weak.lock()) {
				strong->mediaUploaded(media);
			}
		}).fail([weak](const MTP::Error &) {
			if (const auto strong = weak.lock()) {
				strong->mediaUploadFailed();
			}
		}).send();
	}

	void uploadFailed(FullMsgId id) {
		if (_cancelled || !_uploadId || id != _uploadId) {
			return;
		}
		_uploadId = FullMsgId();
		finishCurrentMedia(false);
	}

	void mediaUploaded(const MTPMessageMedia &media) {
		_requestId = 0;
		const auto current = session();
		if (_cancelled || !current) {
			return;
		}
		auto success = false;
		media.match([&](const MTPDmessageMediaPhoto &data) {
			const auto photo = data.vphoto();
			if (_currentPhotoId
				&& photo
				&& photo->type() == mtpc_photo) {
				_uploaded.photos.emplace(
					_currentPhotoId,
					current->data().processPhoto(*photo));
				success = true;
			}
		}, [&](const MTPDmessageMediaDocument &data) {
			const auto document = data.vdocument();
			if (_currentDocumentId
				&& document
				&& document->type() == mtpc_document) {
				_uploaded.documents.emplace(
					_currentDocumentId,
					current->data().processDocument(*document));
				success = true;
			}
		}, [](const auto &) {
		});
		finishCurrentMedia(success);
	}

	void mediaUploadFailed() {
		_requestId = 0;
		finishCurrentMedia(false);
	}

	void finishCurrentMedia(bool success) {
		if (!success) {
			const auto id = _currentPhotoId
				? uint64(_currentPhotoId)
				: uint64(_currentDocumentId);
			LOG(("AyuForward: failed to transfer rich media %1").arg(id));
		}
		_prepared.reset();
		if (_currentPhotoId) {
			++_photoIndex;
		} else if (_currentDocumentId) {
			++_documentIndex;
		}
		_currentPhotoId = 0;
		_currentDocumentId = 0;
		startNextMedia();
	}

	void serializeAndSend() {
		const auto current = session();
		if (_cancelled || !current) {
			complete(false);
			return;
		}
		PruneBlocks(current, _page.blocks, _uploaded);
		if (_page.blocks.empty()) {
			complete(false);
			return;
		}
		const auto page = std::make_shared<Iv::RichPage>(std::move(_page));
		const auto serialized = Iv::SerializeInputRichMessage(
			current,
			*page,
			Iv::SerializeInputRichMessageMode::FinalSubmit);
		if (serialized.status != Iv::SerializeInputRichMessageStatus::Success
			|| !serialized.value) {
			complete(false);
			return;
		}
		const auto weak = weak_from_this();
		auto completion = Api::MakeSendCompletion([weak](bool success) {
			if (const auto strong = weak.lock()) {
				strong->complete(success);
			}
		});
		current->api().sendRichMessage(
			std::move(page),
			*serialized.value,
			_action,
			std::move(completion));
	}

	void complete(bool success) {
		if (_finished || _cancelled) {
			return;
		}
		_finished = true;
		_lifetime.destroy();
		_downloadLifetime.reset();
		if (_completion) {
			base::take(_completion)(success);
		}
	}

	base::weak_ptr<Main::Session> _session;
	FullMsgId _itemId;
	Api::SendAction _action;
	FnMut<void(bool)> _completion;
	Iv::RichPage _page;
	RichMedia _source;
	RichMedia _uploaded;
	rpl::lifetime _lifetime;
	std::unique_ptr<rpl::lifetime> _downloadLifetime;
	std::shared_ptr<FilePrepareResult> _prepared;
	FullMsgId _uploadId;
	TaskId _prepareTask = kEmptyTaskId;
	mtpRequestId _requestId = 0;
	PhotoId _currentPhotoId = 0;
	DocumentId _currentDocumentId = 0;
	int _photoIndex = 0;
	int _documentIndex = 0;
	bool _cancelled = false;
	bool _finished = false;
};

} // namespace

std::shared_ptr<RichForwardRequest> forwardRichMessage(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		const Api::SendAction &action,
		FnMut<void(bool)> completion) {
	const auto result = std::make_shared<Request>(
		session,
		itemId,
		action,
		std::move(completion));
	crl::on_main([weak = std::weak_ptr<Request>(result)] {
		if (const auto strong = weak.lock()) {
			strong->start();
		}
	});
	return result;
}

} // namespace AyuForward
