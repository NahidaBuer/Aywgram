#pragma once

#include "ayu/cloud/ayu_cloud_codec.h"
#include "base/weak_ptr.h"

#include <QtCore/QMap>

#include <optional>
#include <vector>

namespace Main {
class Session;
} // namespace Main

class UserData;

namespace AyuCloud {

class Storage final : public base::has_weak_ptr {
public:
	explicit Storage(not_null<Main::Session*> session);

	using EmptyResult = base::expected<bool, Error>;
	using ValuesResult = base::expected<QMap<QString, QString>, Error>;
	using KeysResult = base::expected<std::vector<QString>, Error>;

	[[nodiscard]] bool available() const;
	void setItem(QString key, QString value, Fn<void(EmptyResult)> done);
	void getItems(std::vector<QString> keys, Fn<void(ValuesResult)> done);
	void deleteItems(std::vector<QString> keys, Fn<void(EmptyResult)> done);
	void getKeys(Fn<void(KeysResult)> done);

private:
	using BotCallback = Fn<void(UserData*, std::optional<Error>)>;

	void withBot(BotCallback callback);
	void finishResolve(UserData *bot, std::optional<Error> error);
	void invoke(
		UserData *bot,
		QString method,
		QByteArray params,
		Fn<void(base::expected<QByteArray, Error>)> done);
	[[nodiscard]] bool validKey(const QString &key) const;

	const not_null<Main::Session*> _session;
	UserData *_resolvedBot = nullptr;
	bool _resolving = false;
	std::vector<BotCallback> _resolveCallbacks;
};

} // namespace AyuCloud
