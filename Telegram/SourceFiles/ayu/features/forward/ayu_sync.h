// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
struct FileOrigin;
} // namespace Data

class DocumentData;
class PhotoData;

namespace rpl {
class lifetime;
} // namespace rpl

namespace AyuSync {

[[nodiscard]] QString pathForSave(not_null<Main::Session*> session);
[[nodiscard]] QString filePath(
	not_null<Main::Session*> session,
	FullMsgId itemId);
[[nodiscard]] QString filePath(
	not_null<Main::Session*> session,
	not_null<PhotoData*> photo);
void loadPhoto(
	not_null<Main::Session*> session,
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	rpl::lifetime &lifetime,
	FnMut<void(QString)> completion);
void loadDocument(
	not_null<Main::Session*> session,
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	rpl::lifetime &lifetime,
	FnMut<void(QString)> completion);
void loadMedia(
	not_null<Main::Session*> session,
	FullMsgId itemId,
	rpl::lifetime &lifetime,
	FnMut<void(bool)> completion);

} // namespace AyuSync
