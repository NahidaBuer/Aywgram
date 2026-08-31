/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_controller.h"

#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/qt_signal_producer.h"
#include "core/file_utilities.h"
#include "data/data_types.h"
#include "lang_auto.h"
#include "lang/lang_keys.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/rp_window.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "webview/webview_embed.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_interface.h"
#include "styles/palette.h"
#include "styles/style_iv.h"
#include "styles/style_payments.h"
#include "styles/style_window.h"
#include "window/themes/window_theme.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtGui/QWindow>

#include <string>

#include <ada.h>

// AyuGram includes
#include "ayu/ayu_settings.h"
#include "ayu/features/streamer_mode/streamer_mode.h"


namespace Iv {
namespace {

constexpr auto kZoomStep = int(10);
constexpr auto kWaylandWebviewTeardownDelay = crl::time(1000);
constexpr auto kTLViewerDocument = "tlv/tlv.html";

[[nodiscard]] QByteArray ReadTLViewerResource(const QString &name) {
	auto file = QFile(u":/tlv/"_q + name);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] int SchemaLayer(const QByteArray &schema) {
	const auto marker = QByteArray("// LAYER ");
	const auto position = schema.lastIndexOf(marker);
	if (position < 0) {
		return 0;
	}
	return schema.mid(position + marker.size()).split('\n').front().trimmed().toInt();
}

[[nodiscard]] QString TonsiteToHttps(QString value) {
	const auto ChangeHost = [](QString tonsite) {
		const auto fake = "http://" + tonsite.toStdString();
		const auto parsed = ada::parse<ada::url>(fake);
		if (!parsed) {
			return QString();
		}
		tonsite = QString::fromStdString(parsed->get_hostname());
		tonsite = tonsite.replace('-', "-h");
		tonsite = tonsite.replace('.', "-d");
		return tonsite + ".magic.org";
	};
	const auto prefix = u"tonsite://"_q;
	if (!value.toLower().startsWith(prefix)) {
		return QString();
	}
	const auto part = value.mid(prefix.size());
	const auto split = part.indexOf('/');
	const auto host = ChangeHost((split < 0) ? part : part.left(split));
	if (host.isEmpty()) {
		return QString();
	}
	return "https://" + host + ((split < 0) ? u"/"_q : part.mid(split));
}

[[nodiscard]] QString HttpsToTonsite(QString value) {
	const auto ChangeHost = [](QString https) {
		const auto dot = https.indexOf('.');
		if (dot < 0 || https.mid(dot).toLower() != u".magic.org"_q) {
			return QString();
		}
		https = https.mid(0, dot);
		https = https.replace("-d", ".");
		https = https.replace("-h", "-");
		auto parts = https.split('.');
		for (auto &part : parts) {
			if (part.startsWith(u"xn--"_q)) {
				const auto utf8 = part.mid(4).toStdString();
				auto out = std::u32string();
				if (ada::idna::punycode_to_utf32(utf8, out)) {
					part = QString::fromUcs4(out.data(), out.size());
				}
			}
		}
		return parts.join('.');
	};
	const auto prefix = u"https://"_q;
	if (!value.toLower().startsWith(prefix)) {
		return value;
	}
	const auto part = value.mid(prefix.size());
	const auto split = part.indexOf('/');
	const auto host = ChangeHost((split < 0) ? part : part.left(split));
	if (host.isEmpty()) {
		return value;
	}
	return "tonsite://"
		+ host
		+ ((split < 0) ? u"/"_q : part.mid(split));
}

} // namespace

Controller::Controller(not_null<Delegate*> delegate)
: _delegate(delegate) {
	createWindow();
}

Controller::~Controller() {
	if (_window) {
		_window->hide();
	}
	base::take(_webview);
	_back = nullptr;
	_forward = nullptr;
	_subtitle = nullptr;
	_subtitleWrap = nullptr;
	_window = nullptr;
}

void Controller::updateTitleGeometry(int newWidth) const {
	_subtitleWrap->setGeometry(
		0,
		0,
		newWidth,
		st::ivSubtitleHeight);
	_subtitleWrap->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(_subtitleWrap.get()).fillRect(clip, st::windowBg);
	}, _subtitleWrap->lifetime());

	const auto left = (_back ? _back->width() : 0)
		+ (_forward ? _forward->width() : 0)
		+ st::ivSubtitleSkip;
	const auto right = st::ivSubtitleSkip;
	_subtitle->resizeToWidth(std::max(newWidth - left - right, 0));
	_subtitle->moveToLeft(left, st::ivSubtitleTop);

	if (_back) {
		_back->moveToLeft(0, 0);
	}
	if (_forward) {
		_forward->moveToLeft(_back ? _back->width() : 0, 0);
	}
}

bool Controller::IsGoodTonSiteUrl(const QString &uri) {
	return !TonsiteToHttps(uri).isEmpty();
}

void Controller::showTonSite(
		const Webview::StorageId &storageId,
		QString uri) {
	const auto url = TonsiteToHttps(uri);
	Assert(!url.isEmpty());

	if (!_webview) {
		createWebview(storageId);
	}
	if (_webview && _webview->widget()) {
		_webview->navigate(url);
		activate();
	}
	_url = url;
	_subtitleText = _url.value(
	) | rpl::filter([=](const QString &url) {
		return !url.isEmpty() && url != u"about:blank"_q;
	}) | rpl::map([=](QString value) {
		return HttpsToTonsite(value);
	});
	_windowTitleText = _subtitleText.value();
}

void Controller::showTLViewer(int32 layer, QByteArray payload) {
	_tlViewer = true;
	_tlViewerLayer = layer;
	_tlViewerPayload = std::move(payload);
#ifdef Q_OS_WIN
	_window->setNativeFrame(false);
#endif // Q_OS_WIN
	if (!_webview) {
		// Empty storage creates an isolated, non-persistent webview profile.
		createWebview({});
	}
	if (_webview && _webview->widget()) {
		if (_tlViewerLoaded) {
			loadTLViewerPayload();
		} else {
			_webview->navigateToData(QString::fromLatin1(kTLViewerDocument));
		}
		activate();
	}
	_url = u"tlv/tlv.html"_q;
	_subtitleText = tr::ayu_ViewAsJson(tr::now);
	_windowTitleText = _subtitleText.value();
}

void Controller::createWindow() {
	_window = std::make_unique<Ui::RpWindow>();
	const auto window = _window.get();

	_subtitleWrap = std::make_unique<Ui::RpWidget>(_window->body().get());
	_subtitle = Ui::CreateChild<Ui::FlatLabel>(
		_subtitleWrap.get(),
		_subtitleText.value(),
		st::ivSubtitle);
	_subtitle->setSelectable(true);

	_windowTitleText = _subtitleText.value(
	) | rpl::map([=](const QString &subtitle) {
		const auto prefix = tr::lng_iv_window_title(tr::now);
		return prefix + ' ' + QChar(0x2014) + ' ' + subtitle;
	});
	_windowTitleText.value(
	) | rpl::on_next([=](const QString &title) {
		_window->setWindowTitle(title);
	}, _subtitle->lifetime());

	_back = Ui::CreateChild<Ui::IconButton>(_subtitleWrap.get(), st::ivBack);
	_back->setClickedCallback([=] {
		if (_webview) {
			_webview->eval("window.history.back();");
		}
	});
	_back->setDisabled(true);

	_forward = Ui::CreateChild<Ui::IconButton>(
		_subtitleWrap.get(),
		st::ivForward);
	_forward->setClickedCallback([=] {
		if (_webview) {
			_webview->eval("window.history.forward();");
		}
	});
	_forward->setDisabled(true);

	base::qt_signal_producer(
		qApp,
		&QGuiApplication::focusWindowChanged
	) | rpl::filter([=](QWindow *focused) {
		const auto handle = window->window()->windowHandle();
		return _webview && handle && (focused == handle);
	}) | rpl::on_next([=] {
		setInnerFocus();
	}, window->lifetime());

	window->body()->widthValue() | rpl::on_next([=](int width) {
		updateTitleGeometry(width);
	}, _subtitle->lifetime());

	window->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			close();
		} else if (e->type() == QEvent::KeyPress) {
			processKey(static_cast<QKeyEvent*>(e.get()));
		}
	}, window->lifetime());

	base::install_event_filter(window, qApp, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ShortcutOverride
			|| !window->isActiveWindow()) {
			return base::EventFilterResult::Continue;
		}
		const auto event = static_cast<QKeyEvent*>(e.get());
		const auto command = Platform::IsMac()
			? Qt::MetaModifier
			: Qt::ControlModifier;
		if (!(event->modifiers() & command)) {
			return base::EventFilterResult::Continue;
		} else if (event->key() == Qt::Key_Plus
			|| event->key() == Qt::Key_Equal) {
			_delegate->ivSetZoom(_delegate->ivZoom() + kZoomStep);
			return base::EventFilterResult::Cancel;
		} else if (event->key() == Qt::Key_Minus) {
			_delegate->ivSetZoom(_delegate->ivZoom() - kZoomStep);
			return base::EventFilterResult::Cancel;
		} else if (event->key() == Qt::Key_0) {
			_delegate->ivSetZoom(0);
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	window->setGeometry(_delegate->ivGeometry(window));
	window->setMinimumSize({ st::windowMinWidth, st::windowMinHeight });

	window->geometryValue(
	) | rpl::distinct_until_changed(
	) | rpl::skip(1) | rpl::on_next([=] {
		_delegate->ivSaveGeometry(window);
	}, window->lifetime());

	_container = Ui::CreateChild<Ui::RpWidget>(window->body().get());
	rpl::combine(
		window->body()->sizeValue(),
		_subtitleWrap->heightValue()
	) | rpl::on_next([=](QSize size, int title) {
		_container->setGeometry(QRect(QPoint(), size).marginsRemoved(
			{ 0, title, 0, 0 }));
	}, _container->lifetime());

	_container->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());

	_container->show();
	window->show();
}

void Controller::createWebview(const Webview::StorageId &storageId) {
	Expects(!_webview);

	const auto window = _window.get();

	if (AyuSettings::getInstance().streamerMode()) {
		AyuFeatures::StreamerMode::hideWidgetWindow(window);
	}

	_webview = std::make_unique<Webview::Window>(
		_container,
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.storageId = storageId,
			.safe = true,
		});
	const auto raw = _webview.get();

	if (const auto webviewZoomController = raw->zoomController()) {
		webviewZoomController->zoomValue(
		) | rpl::on_next([this](int value) {
			if (value > 0) {
				_delegate->ivSetZoom(value);
			}
		}, lifetime());
		_delegate->ivZoomValue(
		) | rpl::on_next([=](int value) {
			webviewZoomController->setZoom(value);
		}, lifetime());
		webviewZoomController->setZoom(_delegate->ivZoom());
	}

	window->lifetime().add([=] {
		base::take(_webview);
	});

	const auto widget = raw->widget();
	if (!widget) {
		base::take(_webview);
		showWebviewError();
		return;
	}
	widget->show();

	QObject::connect(widget, &QObject::destroyed, [=] {
		if (!_webview) {
			return;
		}
		crl::on_main(window, [=] {
			showWebviewError({ "Error: WebView has crashed." });
		});
		base::take(_webview);
	});

	_container->sizeValue(
	) | rpl::on_next([=](QSize size) {
		if (const auto widget = raw->widget()) {
			widget->setGeometry(QRect(QPoint(), size));
		}
	}, _container->lifetime());

	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		Q_UNUSED(newWindow);

		if (_tlViewer) {
			const auto url = QUrl(uri);
			const auto localHost = (url.host() == u"desktop-app-resource"_q)
				|| (url.host() == u"127.0.0.1"_q);
			return (uri == u"about:blank"_q)
				|| (localHost && url.path() == u"/tlv/tlv.html"_q);
		}
		if (uri == u"about:blank"_q
			|| QUrl(uri).host().toLower().endsWith(u".magic.org"_q)) {
			return true;
		}
		_events.fire({ .type = Event::Type::OpenLink, .url = uri });
		return false;
	});
	raw->setNavigationDoneHandler([=](bool success) {
		if (_tlViewer && success) {
			_tlViewerLoaded = true;
			loadTLViewerPayload();
		}
	});
	raw->setDataRequestHandler([=](Webview::DataRequest request) {
		if (!_tlViewer) {
			return Webview::DataResult::Failed;
		}
		auto name = QString();
		auto mime = std::string();
		if (request.id == "tlv/tlv.html") {
			name = u"tlv.html"_q;
			mime = "text/html; charset=utf-8";
		} else if (request.id == "tlv/style.css") {
			name = u"style.css"_q;
			mime = "text/css; charset=utf-8";
		} else if (request.id == "tlv/app.js") {
			name = u"app.js"_q;
			mime = "text/javascript; charset=utf-8";
		} else {
			return Webview::DataResult::Failed;
		}
		auto bytes = ReadTLViewerResource(name);
		if (bytes.isEmpty()) {
			return Webview::DataResult::Failed;
		}
		request.done({
			.stream = std::make_unique<Webview::DataStreamFromMemory>(
				std::move(bytes),
				std::move(mime)),
		});
		return Webview::DataResult::Done;
	});
	raw->setMessageHandler([=](Webview::Message message) {
		if (!_tlViewer) {
			return;
		}
		const auto source = QUrl(QString::fromStdString(message.sourceUrl));
		const auto localHost = (source.host() == u"desktop-app-resource"_q)
			|| (source.host() == u"127.0.0.1"_q);
		if (!localHost || source.path() != u"/tlv/tlv.html"_q) {
			return;
		}
		const auto document = QJsonDocument::fromJson(
			QByteArray::fromRawData(message.text.data(), message.text.size()));
		if (!document.isObject()) {
			return;
		}
		const auto object = document.object();
		const auto value = object.value(u"text"_q);
		if (object.value(u"type"_q).toString() != u"clipboard_write"_q
			|| !value.isString()
			|| value.toString().size() > 16 * 1024 * 1024) {
			return;
		}
		QGuiApplication::clipboard()->setText(value.toString());
	});
	raw->navigationHistoryState(
	) | rpl::on_next([=](Webview::NavigationHistoryState state) {
		_back->setDisabled(!state.canGoBack);
		_forward->setDisabled(!state.canGoForward);
		_url = QString::fromStdString(state.url);
	}, _webview->lifetime());

	style::PaletteChanged() | rpl::on_next([=] {
		updateWebviewTheme();
	}, _webview->lifetime());
	updateWebviewTheme();

	raw->init(R"(
		window.open = function() { return null; };
	)");
}

void Controller::loadTLViewerPayload() {
	if (!_tlViewer || !_webview) {
		return;
	}
	const auto schema = ReadTLViewerResource(u"api.tl"_q);
	const auto schemaLayer = SchemaLayer(schema);
	if (schema.isEmpty() || !schemaLayer) {
		showWebviewError({ "Error: Could not load the bundled TL schema." });
		return;
	}
	const auto object = QJsonObject{
		{ u"layer"_q, _tlViewerLayer },
		{ u"schemaLayer"_q, schemaLayer },
		{ u"payload"_q, QString::fromLatin1(_tlViewerPayload) },
		{ u"schema"_q, QString::fromUtf8(schema) },
		{ u"dark"_q, st::windowBg->c.lightness() < 128 },
	};
	const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
	_webview->eval(QByteArray("window.TelegramDesktopTLV.load(")
		+ json
		+ ");");
}

void Controller::updateWebviewTheme() {
	if (!_webview) {
		return;
	}
	const auto params = Window::Theme::WebViewParams();
	_webview->updateTheme(
		params.bodyBg,
		params.scrollBg,
		params.scrollBgOver,
		params.scrollBarBg,
		params.scrollBarBgOver);
	if (_tlViewer) {
		_webview->eval(QByteArray(
			"window.TelegramDesktopTLV&&window.TelegramDesktopTLV.setTheme("
		) + (st::windowBg->c.lightness() < 128 ? "true);" : "false);"));
	}
}

void Controller::processKey(QKeyEvent *event) {
	const auto command = Platform::IsMac()
		? Qt::MetaModifier
		: Qt::ControlModifier;
	if (event->key() == Qt::Key_Escape) {
		close();
	} else if (event->modifiers() & command) {
		if (event->key() == Qt::Key_W) {
			close();
		} else if (event->key() == Qt::Key_M) {
			minimize();
		} else if (event->key() == Qt::Key_Q) {
			quit();
		} else if (event->key() == Qt::Key_0) {
			_delegate->ivSetZoom(0);
		}
	}
}

void Controller::activate() {
	if (_window->isMinimized()) {
		_window->showNormal();
	} else if (_window->isHidden()) {
		_window->show();
	}
	_window->raise();
	_window->activateWindow();
	_window->setFocus();
	setInnerFocus();
}

void Controller::setInnerFocus() {
	if (_webview) {
		_webview->focus();
	}
}

bool Controller::active() const {
	return _window && _window->isActiveWindow();
}

void Controller::minimize() {
	if (_window) {
		_window->setWindowState(_window->windowState()
			| Qt::WindowMinimized);
	}
}

void Controller::destroyWindow() {
	if (!_window) {
		return;
	}
	auto webview = base::take(_webview);
	_window->hide();
	_back = nullptr;
	_forward = nullptr;
	_subtitle = nullptr;
	_subtitleWrap = nullptr;
	_window = nullptr;
	_container = nullptr;
	_tlViewerPayload.clear();
	_tlViewerLoaded = false;
	if (Platform::IsWayland() && webview) {
		base::call_delayed(
			kWaylandWebviewTeardownDelay,
			[webview = std::move(webview)]() mutable {
				webview = nullptr;
			});
	}
}

void Controller::close() {
	_events.fire({ Event::Type::Close });
}

void Controller::quit() {
	_events.fire({ Event::Type::Quit });
}

rpl::lifetime &Controller::lifetime() {
	return _lifetime;
}

void Controller::showWebviewError() {
	const auto available = Webview::Availability();
	if (available.error != Webview::Available::Error::None) {
		showWebviewError(Ui::BotWebView::ErrorText(available));
	} else {
		showWebviewError({ "Error: Could not initialize WebView." });
	}
}

void Controller::showWebviewError(TextWithEntities text) {
	const auto wrap = Ui::CreateChild<Ui::RpWidget>(_container);

	const auto error = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		wrap,
		object_ptr<Ui::FlatLabel>(
			wrap,
			rpl::single(text),
			st::paymentsCriticalError),
		st::paymentsCriticalErrorPadding);
	error->entity()->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton) {
		const auto entity = handler->getTextEntity();
		if (entity.type != EntityType::CustomUrl) {
			return true;
		}
		File::OpenUrl(entity.data);
		return false;
	});
	wrap->show();

	wrap->widthValue() | rpl::on_next([=](int width) {
		error->resizeToWidth(width);
		wrap->resize(width, error->height());
	}, wrap->lifetime());

	_container->sizeValue() | rpl::on_next([=](QSize size) {
		wrap->setGeometry(0, 0, size.width(), size.height() * 2 / 3);
	}, wrap->lifetime());
}

} // namespace Iv
