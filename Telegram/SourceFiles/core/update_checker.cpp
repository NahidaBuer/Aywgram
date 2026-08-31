/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_checker.h"

#include "base/bytes.h"
#include "base/platform/base_platform_file_utilities.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/update_metadata.h"
#include "core/update_unpack.h"
#include "core/version.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "platform/platform_specific.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "settings/sections/settings_advanced.h"
#include "settings/settings_intro.h"
#include "storage/localstorage.h"
#include "ui/layers/box_content.h"

#include <QtCore/QFileSystemWatcher>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>

#include <ksandbox.h>

#include <algorithm>
#include <limits>

#if !defined Q_OS_WIN && !defined Q_OS_MAC
#include "base/platform/linux/base_linux_xdp_utilities.h"

#include <flatpakportal/flatpakportal.hpp>
#endif // !Q_OS_WIN && !Q_OS_MAC

#ifndef Q_OS_WIN
#include <unistd.h>
#endif // !Q_OS_WIN

namespace Core {
namespace {

constexpr auto kUpdaterTimeout = 10 * crl::time(1000);
constexpr auto kMetadataUrl = "https://github.com/NahidaBuer/AywGram/"
	"releases/latest/download/update-metadata.json";

#if !defined Q_OS_WIN && !defined Q_OS_MAC
constexpr auto kFlatpakPortalService = "org.freedesktop.portal.Flatpak";
constexpr auto kFlatpakPortalObjectPath = "/org/freedesktop/portal/Flatpak";
constexpr auto kFlatpakUpdated = "/app/.updated"_cs;
#endif // !Q_OS_WIN && !Q_OS_MAC

#ifdef TDESKTOP_DISABLE_AUTOUPDATE
bool UpdaterIsDisabled = true;
#else // TDESKTOP_DISABLE_AUTOUPDATE
bool UpdaterIsDisabled = false;
#endif // TDESKTOP_DISABLE_AUTOUPDATE

std::weak_ptr<Updater> UpdaterInstance;

using Progress = UpdateChecker::Progress;
using State = UpdateChecker::State;

#ifdef Q_OS_WIN
using VersionInt = DWORD;
#else // Q_OS_WIN
using VersionInt = int;
#endif // Q_OS_WIN

using Loader = MTP::AbstractDedicatedLoader;

#if !defined Q_OS_WIN && !defined Q_OS_MAC
using namespace gi::repository;
namespace GObject = gi::repository::GObject;
#endif // !Q_OS_WIN && !Q_OS_MAC

class Checker : public base::has_weak_ptr {
public:
	Checker(bool testing);

	virtual void start() = 0;

	virtual bool poll() const;

	rpl::producer<std::shared_ptr<Loader>> ready() const;
	rpl::producer<> failed() const;

	rpl::lifetime &lifetime();

	virtual ~Checker() = default;

protected:
	void done(std::shared_ptr<Loader> result);
	void fail();

private:
	rpl::event_stream<std::shared_ptr<Loader>> _ready;
	rpl::event_stream<> _failed;

	rpl::lifetime _lifetime;

};

struct Implementation {
	std::unique_ptr<Checker> checker;
	std::shared_ptr<Loader> loader;
	bool failed = false;

};

class HttpChecker : public Checker {
public:
	HttpChecker(bool testing);

	void start() override;

	~HttpChecker();

private:
	void gotResponse();
	void gotFailure(QNetworkReply::NetworkError e);
	void clearSentRequest();
	bool handleResponse(const QByteArray &response);

	std::unique_ptr<QNetworkAccessManager> _manager;
	QNetworkReply *_reply = nullptr;

};

class HttpLoaderActor;

class HttpLoader : public Loader {
public:
	explicit HttpLoader(UpdateMetadata::Asset asset);

	[[nodiscard]] const UpdateMetadata::Asset &asset() const;

	~HttpLoader();

private:
	void startLoading() override;

	friend class HttpLoaderActor;

	QString _url;
	UpdateMetadata::Asset _asset;
	std::unique_ptr<QThread> _thread;
	HttpLoaderActor *_actor = nullptr;

};

class HttpLoaderActor : public QObject {
public:
	HttpLoaderActor(
		not_null<HttpLoader*> parent,
		not_null<QThread*> thread,
		const QString &url);

private:
	void start();
	void sendRequest();

	void gotMetaData();
	void partFinished(qint64 got, qint64 total);
	void partFailed(QNetworkReply::NetworkError e);

	not_null<HttpLoader*> _parent;
	QString _url;
	QNetworkAccessManager _manager;
	std::unique_ptr<QNetworkReply> _reply;
	bool _metaDataHandled = false;

};

#if !defined Q_OS_WIN && !defined Q_OS_MAC
class FlatpakChecker : public Checker {
public:
	FlatpakChecker(bool testing);

	void start() override;

	bool poll() const override;

	~FlatpakChecker();

private:
	FlatpakPortal::Flatpak _interface;
	FlatpakPortal::FlatpakUpdateMonitor _monitor;
	QFileSystemWatcher _watcher;
	ulong _updateAvailableSignal = 0;

};

class FlatpakLoader : public Loader {
public:
	FlatpakLoader(FlatpakPortal::FlatpakUpdateMonitor monitor);

	~FlatpakLoader();

private:
	void startLoading() override;

	FlatpakPortal::FlatpakUpdateMonitor _monitor;
	ulong _progressSignal = 0;

};
#endif // !Q_OS_WIN && !Q_OS_MAC

std::shared_ptr<Updater> GetUpdaterInstance() {
	if (const auto result = UpdaterInstance.lock()) {
		return result;
	}
	const auto result = std::make_shared<Updater>();
	UpdaterInstance = result;
	return result;
}

QString UpdatesFolder() {
	return cWorkingDir() + u"tupdates"_q;
}

void ClearAll() {
	base::Platform::DeleteDirectory(UpdatesFolder());
}

void ClearLegacyUpdateFiles() {
	const auto expression = QRegularExpression(
		u"^(tupdate|tx64upd|tarm64upd|tmacupd|tarmacupd|tlinuxupd)"
		u"\\d+(_[a-z\\d]+)?$"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto files = QDir(UpdatesFolder()).entryInfoList(QDir::Files);
	for (const auto &file : files) {
		if (expression.match(file.fileName()).hasMatch()) {
			QFile::remove(file.absoluteFilePath());
		}
	}
}

QString FindUpdateFile() {
	const auto path = UpdatesFolder() + u"/download"_q;
	return QFileInfo(path).isFile() ? path : QString();
}

Checker::Checker(bool) {
}

rpl::producer<std::shared_ptr<Loader>> Checker::ready() const {
	return _ready.events();
}

rpl::producer<> Checker::failed() const {
	return _failed.events();
}

bool Checker::poll() const {
	return true;
}

void Checker::done(std::shared_ptr<Loader> result) {
	_ready.fire(std::move(result));
}

void Checker::fail() {
	_failed.fire({});
}

rpl::lifetime &Checker::lifetime() {
	return _lifetime;
}

HttpChecker::HttpChecker(bool testing) : Checker(testing) {
}

void HttpChecker::start() {
	auto url = QUrl(QString::fromLatin1(kMetadataUrl));
	auto query = QUrlQuery();
	const auto period = std::max(int(UpdateDelayConstPart), 1);
	query.addQueryItem(
		u"check"_q,
		QString::number(base::unixtime::now() / period));
	url.setQuery(query);
	DEBUG_LOG(("Update Info: requesting update state"));
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::CacheLoadControlAttribute,
		QNetworkRequest::AlwaysNetwork);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("Cache-Control", "no-cache");
	request.setRawHeader("Pragma", "no-cache");
	_manager = std::make_unique<QNetworkAccessManager>();
	_reply = _manager->get(request);
	_reply->connect(_reply, &QNetworkReply::finished, [=] {
		gotResponse();
	});
	_reply->connect(_reply, &QNetworkReply::errorOccurred, [=](auto e) {
		gotFailure(e);
	});
	_reply->connect(_reply, &QNetworkReply::downloadProgress, [=](qint64 got) {
		if (got > UpdateMetadata::kMaximumMetadataSize) {
			gotFailure(QNetworkReply::UnknownContentError);
		}
	});
}

void HttpChecker::gotResponse() {
	if (!_reply) {
		return;
	}

	cSetLastUpdateCheck(base::unixtime::now());
	const auto response = _reply->readAll();
	clearSentRequest();

	if (response.size() > UpdateMetadata::kMaximumMetadataSize
		|| !handleResponse(response)) {
		LOG(("Update Error: Bad update map size: %1").arg(response.size()));
		gotFailure(QNetworkReply::UnknownContentError);
	}
}

bool HttpChecker::handleResponse(const QByteArray &response) {
	const auto target = UpdateMetadata::CurrentTarget();
	if (target.isEmpty()) {
		done(nullptr);
		return true;
	}
	auto valid = false;
	const auto asset = UpdateMetadata::Parse(response, target, &valid);
	if (!valid) {
		return false;
	}
	if (!asset || !UpdateMetadata::IsNewer(
			{ asset->appVersion, asset->revision },
			{ AppVersion, AppReleaseRevision })) {
		done(nullptr);
	} else {
		done(std::make_shared<HttpLoader>(*asset));
	}
	return true;
}

void HttpChecker::clearSentRequest() {
	const auto reply = base::take(_reply);
	if (!reply) {
		return;
	}
	reply->disconnect(reply, &QNetworkReply::finished, nullptr, nullptr);
	reply->disconnect(reply, &QNetworkReply::errorOccurred, nullptr, nullptr);
	reply->abort();
	reply->deleteLater();
	_manager = nullptr;
}

void HttpChecker::gotFailure(QNetworkReply::NetworkError e) {
	LOG(("Update Error: "
		"could not get current version %1").arg(e));
	clearSentRequest();
	fail();
}

HttpChecker::~HttpChecker() {
	clearSentRequest();
}

HttpLoader::HttpLoader(UpdateMetadata::Asset asset)
: Loader(
	UpdatesFolder() + u"/download"_q,
	kChunkSize,
	UpdateMetadata::kMaximumArchiveSize)
, _url(asset.url)
, _asset(std::move(asset)) {
}

const UpdateMetadata::Asset &HttpLoader::asset() const {
	return _asset;
}

void HttpLoader::startLoading() {
	LOG(("Update Info: Loading using HTTP from '%1'.").arg(_url));

	_thread = std::make_unique<QThread>();
	_actor = new HttpLoaderActor(this, _thread.get(), _url);
	_thread->start();
}

HttpLoader::~HttpLoader() {
	if (const auto thread = base::take(_thread)) {
		if (const auto actor = base::take(_actor)) {
			QObject::connect(
				thread.get(),
				&QThread::finished,
				actor,
				&QObject::deleteLater);
		}
		thread->quit();
		thread->wait();
	}
}

HttpLoaderActor::HttpLoaderActor(
		not_null<HttpLoader*> parent,
		not_null<QThread*> thread,
		const QString &url)
: _parent(parent) {
	_url = url;
	moveToThread(thread);
	_manager.moveToThread(thread);

	connect(thread, &QThread::started, this, [=] { start(); });
}

void HttpLoaderActor::start() {
	sendRequest();
}

void HttpLoaderActor::sendRequest() {
	auto request = QNetworkRequest(_url);
	const auto rangeHeaderValue = "bytes="
		+ QByteArray::number(_parent->alreadySize())
		+ "-";
	request.setRawHeader("Range", rangeHeaderValue);
	request.setAttribute(
		QNetworkRequest::HttpPipeliningAllowedAttribute,
		true);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	_reply.reset(_manager.get(request));
	connect(
		_reply.get(),
		&QNetworkReply::downloadProgress,
		this,
		&HttpLoaderActor::partFinished);
	connect(
		_reply.get(),
		&QNetworkReply::errorOccurred,
		this,
		&HttpLoaderActor::partFailed);
	connect(
		_reply.get(),
		&QNetworkReply::metaDataChanged,
		this,
		&HttpLoaderActor::gotMetaData);
}

void HttpLoaderActor::gotMetaData() {
	const auto status = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (_metaDataHandled || (status != 200 && status != 206)) {
		return;
	}
	_metaDataHandled = true;
	if (status == 200 && _parent->alreadySize() > 0) {
		if (!_parent->wipeOutput()) {
			_parent->threadSafeFailed();
			return;
		}
	} else if (status == 206) {
		const auto range = QString::fromLatin1(
			_reply->rawHeader("Content-Range"));
		const auto match = QRegularExpression(
			u"^bytes \\d+-\\d+/(\\d+)$"_q).match(range);
		if (!match.hasMatch()
			|| match.captured(1).toLongLong() != _parent->asset().size) {
			_parent->threadSafeFailed();
			return;
		}
	}
	_parent->writeChunk({}, int(_parent->asset().size));
}

void HttpLoaderActor::partFinished(qint64 got, qint64 total) {
	if (!_reply) return;

	const auto statusCode = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		const auto status = statusCode.toInt();
		if (status == 301
			|| status == 302
			|| status == 303
			|| status == 307
			|| status == 308
			|| status == 416) {
			return;
		}
		if (status != 200 && status != 206) {
			LOG(("Update Error: "
				"Bad HTTP status received in partFinished(): %1"
				).arg(status));
			_parent->threadSafeFailed();
			return;
		}
	}
	if (!_metaDataHandled) {
		gotMetaData();
		if (!_metaDataHandled) {
			_parent->threadSafeFailed();
			return;
		}
	}

	DEBUG_LOG(("Update Info: part %1 of %2").arg(got).arg(total));

	const auto data = _reply->readAll();
	if (_parent->alreadySize() + data.size() > _parent->asset().size) {
		_parent->threadSafeFailed();
		return;
	}
	_parent->writeChunk(
		bytes::make_span(data),
		int(_parent->asset().size));
}

void HttpLoaderActor::partFailed(QNetworkReply::NetworkError e) {
	if (!_reply) return;

	const auto statusCode = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	_reply.release()->deleteLater();
	if (statusCode.isValid()) {
		const auto status = statusCode.toInt();
		if (status == 416) { // Requested range not satisfiable
			_parent->writeChunk({}, int(_parent->alreadySize()));
			return;
		}
	}
	LOG(("Update Error: failed to download part after %1, error %2"
		).arg(_parent->alreadySize()
		).arg(e));
	_parent->threadSafeFailed();
}

#if !defined Q_OS_WIN && !defined Q_OS_MAC
FlatpakChecker::FlatpakChecker(bool testing)
: Checker(testing)
, _watcher({u"/app"_q}) {
	FlatpakPortal::FlatpakProxy::new_for_bus(
			Gio::BusType::SESSION_,
			Gio::DBusProxyFlags::NONE_,
			kFlatpakPortalService,
			kFlatpakPortalObjectPath,
			crl::guard(this, [=](GObject::Object, Gio::AsyncResult res) {
		auto result = FlatpakPortal::FlatpakProxy::new_for_bus_finish(res);
		if (!result) {
			Gio::DBusErrorNS_::strip_remote_error(result.error());
			LOG(("Update Error: %1").arg(result.error().message_().c_str()));
			return;
		}

		_interface = *result;
		_interface.call_create_update_monitor(
				GLib::Variant::new_array(
					GLib::VariantType::new_("{sv}"),
					{}),
				[=](GObject::Object, Gio::AsyncResult res) {
			const auto result = _interface.call_create_update_monitor_finish(
				res);

			if (!result) {
				Gio::DBusErrorNS_::strip_remote_error(result.error());
				LOG(("Update Error: %1").arg(
					result.error().message_().c_str()));
				fail();
				return;
			}

			FlatpakPortal::FlatpakUpdateMonitorProxy::new_for_bus(
					Gio::BusType::SESSION_,
					Gio::DBusProxyFlags::NONE_,
					kFlatpakPortalService,
					std::get<1>(*result),
					crl::guard(this, [=](GObject::Object, Gio::AsyncResult res) {
				using FlatpakPortal::FlatpakUpdateMonitorProxy;
				auto result = FlatpakUpdateMonitorProxy::new_for_bus_finish(
					res);

				if (!result) {
					Gio::DBusErrorNS_::strip_remote_error(result.error());
					LOG(("Update Error: %1").arg(
						result.error().message_().c_str()));
					fail();
					return;
				}

				_monitor = *result;
				_updateAvailableSignal
					= _monitor.signal_update_available().connect([=](
							FlatpakPortal::FlatpakUpdateMonitor,
							GLib::Variant updateInfo) {
						done(std::make_shared<FlatpakLoader>(_monitor));
					});
			}));
		});
	}));

	QObject::connect(
		&_watcher,
		&QFileSystemWatcher::directoryChanged,
		[=](const QString &path) {
			start();
		});
}

void FlatpakChecker::start() {
	if (QFileInfo::exists(kFlatpakUpdated.utf16())) {
		done(std::make_shared<FlatpakLoader>(_monitor));
	}
}

bool FlatpakChecker::poll() const {
	return false;
}

FlatpakChecker::~FlatpakChecker() {
	if (_monitor) {
		_monitor.disconnect(_updateAvailableSignal);
		_monitor.call_close(nullptr);
	}
}

FlatpakLoader::FlatpakLoader(FlatpakPortal::FlatpakUpdateMonitor monitor)
: Loader({}, kChunkSize)
, _monitor(monitor) {
	if (!_monitor) {
		return;
	}

	_progressSignal = _monitor.signal_progress().connect([=](
			FlatpakPortal::FlatpakUpdateMonitor,
			GLib::Variant info) {
		auto dict = GLib::VariantDict::new_(info);
		switch (dict.lookup_value("status").get_uint32()) {
		case 0: {
			const auto n_ops = dict.lookup_value("n_ops").get_uint32();
			const auto op = dict.lookup_value("op").get_uint32();
			const auto progress = dict.lookup_value("progress").get_uint32();
			threadSafeProgress({
				int64(
					std::round((op + (progress / 100.)) / n_ops * 104857600)),
				104857600,
				true,
			});
		} break;
		case 1:
		case 2: threadSafeReady(); break;
		case 3: {
			LOG(("Update Error: %1").arg(
				dict.lookup_value("error_message").get_string(
					nullptr).c_str()));
			threadSafeFailed();
		} break;
		}
	});
}

void FlatpakLoader::startLoading() {
	if (QFileInfo::exists(kFlatpakUpdated.utf16())) {
		threadSafeReady();
	}

	if (!_monitor) {
		return;
	}

	_monitor.call_update(
		base::Platform::XDP::ParentWindowID(),
		GLib::Variant::new_array(
			GLib::VariantType::new_("{sv}"),
			{}),
		crl::guard(this, [=](GObject::Object, Gio::AsyncResult res) {
			const auto result = _monitor.call_close_finish(res);
			if (!result) {
				Gio::DBusErrorNS_::strip_remote_error(result.error());
				LOG(("Update Error: %1").arg(
					result.error().message_().c_str()));
				threadSafeFailed();
			}
		}));
}

FlatpakLoader::~FlatpakLoader() {
	if (_monitor) {
		_monitor.disconnect(_progressSignal);
	}
}
#endif // !Q_OS_WIN && !Q_OS_MAC

} // namespace

bool UpdaterDisabled() {
	return UpdaterIsDisabled;
}

void SetUpdaterDisabledAtStartup() {
	Expects(UpdaterInstance.lock() == nullptr);

	UpdaterIsDisabled = true;
}

class Updater : public base::has_weak_ptr {
public:
	Updater();

	rpl::producer<> checking() const;
	rpl::producer<> isLatest() const;
	rpl::producer<Progress> progress() const;
	rpl::producer<> failed() const;
	rpl::producer<> ready() const;

	void start(bool forceWait);
	void stop();
	void test();

	State state() const;
	int64 already() const;
	int64 size() const;
	bool percent() const;

	~Updater();

private:
	enum class Action {
		Waiting,
		Checking,
		Loading,
		Unpacking,
		Ready,
	};
	void check();
	void startImplementation(
		not_null<Implementation*> which,
		std::unique_ptr<Checker> checker);
	bool tryLoaders();
	void handleTimeout();
	void checkerDone(
		not_null<Implementation*> which,
		std::shared_ptr<Loader> loader);
	void checkerFail(not_null<Implementation*> which);

	void finalize(QString filepath);
	void unpackDone(bool ready);
	void handleChecking();
	void handleProgress();
	void handleLatest();
	void handleFailed();
	void handleReady();
	void scheduleNext();

	bool _testing = false;
	Action _action = Action::Waiting;
	base::Timer _timer;
	base::Timer _retryTimer;
	rpl::event_stream<> _checking;
	rpl::event_stream<> _isLatest;
	rpl::event_stream<Progress> _progress;
	rpl::event_stream<> _failed;
	rpl::event_stream<> _ready;
	Implementation _httpImplementation;
	Implementation _flatpakImplementation;
	std::shared_ptr<Loader> _activeLoader;

	rpl::lifetime _lifetime;

};

Updater::Updater()
: _timer([=] { check(); })
, _retryTimer([=] { handleTimeout(); }) {
	checking() | rpl::on_next([=] {
		handleChecking();
	}, _lifetime);
	progress() | rpl::on_next([=] {
		handleProgress();
	}, _lifetime);
	failed() | rpl::on_next([=] {
		handleFailed();
	}, _lifetime);
	ready() | rpl::on_next([=] {
		handleReady();
	}, _lifetime);
	isLatest() | rpl::on_next([=] {
		handleLatest();
	}, _lifetime);
}

rpl::producer<> Updater::checking() const {
	return _checking.events();
}

rpl::producer<> Updater::isLatest() const {
	return _isLatest.events();
}

auto Updater::progress() const
-> rpl::producer<Progress> {
	return _progress.events();
}

rpl::producer<> Updater::failed() const {
	return _failed.events();
}

rpl::producer<> Updater::ready() const {
	return _ready.events();
}

void Updater::check() {
	start(false);
}

void Updater::handleReady() {
	stop();
	_action = Action::Ready;
	if (!Quitting()) {
		cSetLastUpdateCheck(base::unixtime::now());
		Local::writeSettings();
	}
}

void Updater::handleFailed() {
	scheduleNext();
}

void Updater::handleLatest() {
	if (const auto update = FindUpdateFile(); !update.isEmpty()) {
		QFile(update).remove();
	}
	scheduleNext();
}

void Updater::handleChecking() {
	_action = Action::Checking;
	_retryTimer.callOnce(kUpdaterTimeout);
}

void Updater::handleProgress() {
	_retryTimer.callOnce(kUpdaterTimeout);
}

void Updater::scheduleNext() {
	stop();
	if (!Quitting()) {
		cSetLastUpdateCheck(base::unixtime::now());
		Local::writeSettings();
		start(true);
	}
}

auto Updater::state() const -> State {
	if (_action == Action::Ready) {
		return State::Ready;
	} else if (_action == Action::Loading) {
		return State::Download;
	}
	return State::None;
}

int64 Updater::size() const {
	return _activeLoader ? _activeLoader->totalSize() : 0;
}

int64 Updater::already() const {
	return _activeLoader ? _activeLoader->alreadySize() : 0;
}

bool Updater::percent() const {
	return _activeLoader ? _activeLoader->preferPercent() : 0;
}

void Updater::stop() {
	_httpImplementation = Implementation();
	_flatpakImplementation = Implementation{
		std::move(_flatpakImplementation.checker)
	};
	_activeLoader = nullptr;
	_action = Action::Waiting;
}

void Updater::start(bool forceWait) {
	if (cExeName().isEmpty()) {
		return;
	}

	_timer.cancel();
	if (!cAutoUpdate() || _action != Action::Waiting) {
		return;
	}

	_retryTimer.cancel();
	const auto constDelay = UpdateDelayConstPart;
	const auto randDelay = UpdateDelayRandPart;
	const auto updateInSecs = cLastUpdateCheck()
		+ constDelay
		+ int(rand() % randDelay)
		- base::unixtime::now();
	auto sendRequest = (updateInSecs <= 0)
		|| (updateInSecs > constDelay + randDelay);
	if (!sendRequest && !forceWait) {
		if (!FindUpdateFile().isEmpty()) {
			sendRequest = true;
		}
	}
	if (cManyInstance() && !Logs::DebugEnabled()) {
		// Only main instance is updating.
		return;
	}

	if (KSandbox::isFlatpak()) {
#if !defined Q_OS_WIN && !defined Q_OS_MAC
		if (!_flatpakImplementation.checker) {
			startImplementation(
				&_flatpakImplementation,
				std::make_unique<FlatpakChecker>(_testing));
		}
#endif // !Q_OS_WIN && !Q_OS_MAC
	} else if (sendRequest) {
		startImplementation(
			&_httpImplementation,
			std::make_unique<HttpChecker>(_testing));
		_checking.fire({});
	} else {
		_timer.callOnce((updateInSecs + 5) * crl::time(1000));
	}
}

void Updater::startImplementation(
		not_null<Implementation*> which,
		std::unique_ptr<Checker> checker) {
	if (!checker) {
		class EmptyChecker : public Checker {
		public:
			EmptyChecker() : Checker(false) {
			}

			void start() override {
				crl::on_main(this, [=] { fail(); });
			}

		};
		checker = std::make_unique<EmptyChecker>();
	}

	checker->ready(
	) | rpl::on_next([=](std::shared_ptr<Loader> &&loader) {
		checkerDone(which, std::move(loader));
	}, checker->lifetime());
	checker->failed(
	) | rpl::on_next([=] {
		checkerFail(which);
	}, checker->lifetime());

	*which = Implementation{ std::move(checker) };

	crl::on_main(which->checker.get(), [=] {
		which->checker->start();
	});
}

void Updater::checkerDone(
		not_null<Implementation*> which,
		std::shared_ptr<Loader> loader) {
	if (which->checker->poll()) which->checker = nullptr;
	which->loader = std::move(loader);

	tryLoaders();
}

void Updater::checkerFail(not_null<Implementation*> which) {
	which->checker = nullptr;
	which->failed = true;

	tryLoaders();
}

void Updater::test() {
	_testing = true;
	cSetLastUpdateCheck(0);
	start(false);
}

void Updater::handleTimeout() {
	if (_action == Action::Checking) {
		const auto reset = [&](Implementation &which) {
			if (base::take(which.checker)) {
				which.failed = true;
			}
		};
		reset(_httpImplementation);
		if (!tryLoaders()) {
			cSetLastUpdateCheck(0);
			_timer.callOnce(kUpdaterTimeout);
		}
	} else if (_action == Action::Loading) {
		_failed.fire({});
	}
}

bool Updater::tryLoaders() {
	if (_httpImplementation.checker) {
		return true;
	}
	_retryTimer.cancel();

	const auto tryOne = [&](Implementation &which) {
		_activeLoader = std::move(which.loader);
		if (const auto loader = _activeLoader.get()) {
			_action = Action::Loading;

			loader->progress(
			) | rpl::start_to_stream(_progress, loader->lifetime());
			loader->ready(
			) | rpl::on_next([=](QString &&filepath) {
				finalize(std::move(filepath));
			}, loader->lifetime());
			loader->failed(
			) | rpl::on_next([=] {
				_failed.fire({});
			}, loader->lifetime());

			_retryTimer.callOnce(kUpdaterTimeout);
			loader->wipeFolder();
			loader->start();
		} else {
			_isLatest.fire({});
		}
	};
	if (KSandbox::isFlatpak()) {
		if (_flatpakImplementation.failed) {
			_failed.fire({});
			return false;
		} else {
			tryOne(_flatpakImplementation);
		}
	} else if (_httpImplementation.failed) {
		_failed.fire({});
		return false;
	} else {
		tryOne(_httpImplementation);
	}
	return true;
}

void Updater::finalize(QString filepath) {
	if (_action != Action::Loading) {
		return;
	}
	_retryTimer.cancel();
	const auto http = std::dynamic_pointer_cast<HttpLoader>(_activeLoader);
	const auto asset = http
		? std::optional<UpdateMetadata::Asset>(http->asset())
		: std::nullopt;
	_activeLoader = nullptr;
	_action = Action::Unpacking;
	crl::async([=] {
		const auto ready = asset
			? UnpackReleaseUpdate(filepath, *asset)
			: filepath.isEmpty();
		crl::on_main([=] {
			GetUpdaterInstance()->unpackDone(ready);
		});
	});
}

void Updater::unpackDone(bool ready) {
	if (ready) {
		_ready.fire({});
	} else {
		ClearAll();
		_failed.fire({});
	}
}

Updater::~Updater() {
	stop();
}

UpdateChecker::UpdateChecker()
: _updater(GetUpdaterInstance()) {
}

rpl::producer<> UpdateChecker::checking() const {
	return _updater->checking();
}

rpl::producer<> UpdateChecker::isLatest() const {
	return _updater->isLatest();
}

auto UpdateChecker::progress() const
-> rpl::producer<Progress> {
	return _updater->progress();
}

rpl::producer<> UpdateChecker::failed() const {
	return _updater->failed();
}

rpl::producer<> UpdateChecker::ready() const {
	return _updater->ready();
}

void UpdateChecker::start(bool forceWait) {
	_updater->start(forceWait);
}

void UpdateChecker::test() {
	_updater->test();
}

void UpdateChecker::stop() {
	_updater->stop();
}

auto UpdateChecker::state() const
-> State {
	return _updater->state();
}

int64 UpdateChecker::already() const {
	return _updater->already();
}

int64 UpdateChecker::size() const {
	return _updater->size();
}

bool UpdateChecker::percent() const {
	return _updater->percent();
}

bool checkReadyUpdate() {
	ClearLegacyUpdateFiles();
	const auto readyPath = cWorkingDir() + u"tupdates/temp"_q;
	const auto readyFilePath = readyPath + u"/ready"_q;
	const auto currentTarget = UpdateMetadata::CurrentTarget();
	if (!QFileInfo(readyFilePath).isFile()
		|| cExeName().isEmpty()
		|| currentTarget.isEmpty()) {
		if (QDir(cWorkingDir() + u"tupdates/ready"_q).exists() || QDir(cWorkingDir() + u"tupdates/temp"_q).exists()) {
			ClearAll();
		}
		return false;
	}

	const auto manifestPath = readyPath + u"/update-metadata.json"_q;
	auto manifestFile = QFile(manifestPath);
	if (!manifestFile.open(QIODevice::ReadOnly)) {
		ClearAll();
		return false;
	}
	auto manifestError = QJsonParseError();
	const auto manifestData = manifestFile.read(
		UpdateMetadata::kMaximumMetadataSize + 1);
	const auto manifest = QJsonDocument::fromJson(manifestData, &manifestError);
	const auto manifestObject = manifest.object();
	const auto manifestSchema = manifestObject.value(u"schema"_q);
	const auto manifestAppVersionValue = manifestObject.value(u"app_version"_q);
	const auto manifestRevisionValue = manifestObject.value(u"revision"_q);
	const auto manifestSizeValue = manifestObject.value(u"size"_q);
	const auto manifestAppVersion = manifestAppVersionValue.toDouble();
	const auto manifestRevision = manifestRevisionValue.toDouble();
	const auto manifestVersionName = manifestObject.value(
		u"version_name"_q).toString();
	const auto manifestSize = manifestObject.value(u"size"_q).toDouble();
	const auto manifestFormat = manifestObject.value(u"format"_q).toString();
	const auto manifestHash = manifestObject.value(u"sha256"_q).toString();
	const auto manifestRelease = manifestObject.value(u"release"_q).toString();
	const auto expectedFormat = currentTarget.startsWith(
		u"linux-"_q) ? u"tar.gz"_q : u"zip"_q;
	if (manifestData.size() > UpdateMetadata::kMaximumMetadataSize
		|| manifestError.error != QJsonParseError::NoError
		|| !manifest.isObject()
		|| !manifestSchema.isDouble()
		|| (manifestSchema.toDouble() != 1.)
		|| (manifestObject.value(u"target"_q).toString()
			!= currentTarget)
		|| !manifestAppVersionValue.isDouble()
		|| (manifestAppVersion <= 0)
		|| (manifestAppVersion > std::numeric_limits<int32>::max())
		|| (manifestAppVersion != double(int(manifestAppVersion)))
		|| !manifestRevisionValue.isDouble()
		|| (manifestRevision < 1)
		|| (manifestRevision > 99)
		|| (manifestRevision != double(int(manifestRevision)))
		|| !UpdateMetadata::ValidVersion(
			int(manifestAppVersion),
			int(manifestRevision),
			manifestVersionName)
		|| !UpdateMetadata::IsNewer(
			{ int(manifestAppVersion), int(manifestRevision) },
			{ AppVersion, AppReleaseRevision })
		|| (manifestRelease != u"pre-release-v"_q + manifestVersionName)
		|| !QRegularExpression(
			u"^[0-9A-Za-z][0-9A-Za-z._-]*$"_q).match(
			manifestRelease).hasMatch()
		|| (manifestFormat != expectedFormat)
		|| !manifestSizeValue.isDouble()
		|| (manifestSize <= 0)
		|| (manifestSize > UpdateMetadata::kMaximumArchiveSize)
		|| (manifestSize != double(uint64(manifestSize)))
		|| !QRegularExpression(u"^[0-9a-f]{64}$"_q).match(
			manifestHash).hasMatch()) {
		ClearAll();
		return false;
	}

	const auto versionPath = readyPath + u"/tdata/version"_q;
	{
		QFile fVersion(versionPath);
		if (!fVersion.open(QIODevice::ReadOnly)) {
			LOG(("Update Error: cant read version file '%1'").arg(versionPath));
			ClearAll();
			return false;
		}
		auto versionNum = VersionInt();
		if (fVersion.read((char*)&versionNum, sizeof(VersionInt)) != sizeof(VersionInt)) {
			LOG(("Update Error: cant read version from file '%1'").arg(versionPath));
			ClearAll();
			return false;
		}
		if (versionNum != VersionInt(manifestAppVersion)) {
			LOG(("Update Error: cant install version %1 having version %2").arg(versionNum).arg(AppVersion));
			ClearAll();
			return false;
		}
		fVersion.close();
	}

#ifdef Q_OS_WIN
	QString curUpdater = (cExeDir() + u"Updater.exe"_q);
	QFileInfo updater(cWorkingDir() + u"tupdates/temp/Updater.exe"_q);
#elif defined Q_OS_MAC // Q_OS_WIN
	QString curUpdater = (cExeDir() + cExeName() + u"/Contents/Frameworks/Updater"_q);
	QFileInfo updater(cWorkingDir() + u"tupdates/temp/AywGram.app/Contents/Frameworks/Updater"_q);
#else // Q_OS_MAC
	QString curUpdater = (cExeDir() + u"Updater"_q);
	QFileInfo updater(cWorkingDir() + u"tupdates/temp/Updater"_q);
#endif // else for Q_OS_WIN || Q_OS_MAC
	if (!updater.exists()) {
		QFileInfo current(curUpdater);
		if (!current.exists()) {
			ClearAll();
			return false;
		}
		if (!QFile(current.absoluteFilePath()).copy(updater.absoluteFilePath())) {
			ClearAll();
			return false;
		}
	}
#ifdef Q_OS_WIN
	if (CopyFile(updater.absoluteFilePath().toStdWString().c_str(), curUpdater.toStdWString().c_str(), FALSE) == FALSE) {
		DWORD errorCode = GetLastError();
		if (errorCode == ERROR_ACCESS_DENIED) { // we are in write-protected dir, like Program Files
			cSetWriteProtected(true);
			return true;
		} else {
			ClearAll();
			return false;
		}
	}
	if (DeleteFile(updater.absoluteFilePath().toStdWString().c_str()) == FALSE) {
		ClearAll();
		return false;
	}
#elif defined Q_OS_MAC // Q_OS_WIN
	QDir().mkpath(QFileInfo(curUpdater).absolutePath());
	DEBUG_LOG(("Update Info: moving %1 to %2...").arg(updater.absoluteFilePath()).arg(curUpdater));
	if (!objc_moveFile(updater.absoluteFilePath(), curUpdater)) {
		ClearAll();
		return false;
	}
#else // Q_OS_MAC
	// if the files in the directory are owned by user, while the directory is not,
	// update will still fail since it's not possible to remove files
	if (QFile::exists(curUpdater)
		&& unlink(QFile::encodeName(curUpdater).constData())) {
		if (errno == EACCES) {
			DEBUG_LOG(("Update Info: "
				"could not unlink current Updater, access denied."));
			cSetWriteProtected(true);
			return true;
		} else {
			DEBUG_LOG(("Update Error: could not unlink current Updater."));
			ClearAll();
			return false;
		}
	}
	if (!linuxMoveFile(QFile::encodeName(updater.absoluteFilePath()).constData(), QFile::encodeName(curUpdater).constData())) {
		if (errno == EACCES) {
			DEBUG_LOG(("Update Info: "
				"could not copy new Updater, access denied."));
			cSetWriteProtected(true);
			return true;
		} else {
			DEBUG_LOG(("Update Error: could not copy new Updater."));
			ClearAll();
			return false;
		}
	}
#endif // else for Q_OS_WIN || Q_OS_MAC

#ifdef Q_OS_MAC
	base::Platform::RemoveQuarantine(QFileInfo(curUpdater).absolutePath());
	base::Platform::RemoveQuarantine(updater.absolutePath());
#endif // Q_OS_MAC

	return true;
}

void UpdateApplication() {
	if (UpdaterDisabled()) {
		const auto url = [&] {
#ifdef OS_WIN_STORE
			return "https://github.com/NahidaBuer/AywGram/releases";
#elif defined OS_MAC_STORE // OS_WIN_STORE
			return "https://github.com/NahidaBuer/AywGram/releases";
#else // OS_WIN_STORE || OS_MAC_STORE
			if (KSandbox::isFlatpak()) {
				return "https://flathub.org/apps/details/com.aywgram.desktop";
			} else if (KSandbox::isSnap()) {
				return "https://github.com/NahidaBuer/AywGram/releases";
			}
			return "https://github.com/NahidaBuer/AywGram/releases";
#endif // OS_WIN_STORE || OS_MAC_STORE
		}();
		UrlClickHandler::Open(url);
	} else {
		cSetAutoUpdate(true);
		const auto window = Core::IsAppLaunched()
			? Core::App().activePrimaryWindow()
			: nullptr;
		if (window) {
			if (const auto controller = window->sessionController()) {
				controller->showSection(
					std::make_shared<Info::Memento>(
						Info::Settings::Tag{ controller->session().user() },
						::Settings::AdvancedId()),
					Window::SectionShow());
			} else {
				window->widget()->showSpecialLayer(
					Box<::Settings::LayerWidget>(window),
					anim::type::normal);
			}
			window->widget()->showFromTray();
		}
		cSetLastUpdateCheck(0);
		Core::UpdateChecker().start();
	}
}

} // namespace Core
