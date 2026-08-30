/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/crash_report_window.h"

#include "core/crash_reports.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "core/update_checker.h"
#include "core/ui_integration.h"
#include "window/main_window.h"
#include "platform/platform_specific.h"
#include "base/zlib_help.h"

#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMenu>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QFontInfo>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QDesktopServices>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>

namespace {

constexpr auto kDefaultProxyPort = 80;

} // namespace

PreLaunchWindow *PreLaunchWindowInstance = nullptr;

PreLaunchWindow::PreLaunchWindow(QString title) {
	setWindowIcon(Window::CreateIcon());
	setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

	setWindowTitle(title.isEmpty() ? u"AywGram"_q : title);

	QPalette p(palette());
	p.setColor(QPalette::Window, QColor(255, 255, 255));
	setPalette(p);

	_size = QFontInfo(font()).pixelSize();

	int paddingVertical = (_size / 2);
	int paddingHorizontal = _size;
	int borderRadius = (_size / 5);
	setStyleSheet(uR"(
QPushButton {
	padding: %1px %2px;
	background-color: #ffffff;
	border-radius: %3px;
}
QPushButton#confirm:hover,
QPushButton#cancel:hover {
	background-color: #e3f1fa;
	color: #2f9fea;
}
QPushButton#confirm {
	color: #2f9fea;
}
QPushButton#cancel {
	color: #aeaeae;
}
QLineEdit {
	border: 1px solid #e0e0e0;
	padding: 5px;
}
QLineEdit:focus {
	border: 2px solid #37a1de;
	padding: 4px;
}
)"_q.arg(paddingVertical).arg(paddingHorizontal).arg(borderRadius));
	if (!PreLaunchWindowInstance) {
		PreLaunchWindowInstance = this;
	}
}

void PreLaunchWindow::activate() {
	setWindowState(windowState() & ~Qt::WindowMinimized);
	setVisible(true);
	Platform::ActivateThisProcess();
	raise();
	activateWindow();
}

PreLaunchWindow *PreLaunchWindow::instance() {
	return PreLaunchWindowInstance;
}

PreLaunchWindow::~PreLaunchWindow() {
	if (PreLaunchWindowInstance == this) {
		PreLaunchWindowInstance = nullptr;
	}
}

PreLaunchLabel::PreLaunchLabel(QWidget *parent) : QLabel(parent) {
	QFont labelFont(font());
	labelFont.setWeight(QFont::DemiBold);
	labelFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(labelFont);

	QPalette p(palette());
	p.setColor(QPalette::WindowText, QColor(0, 0, 0));
	p.setColor(QPalette::Text, QColor(0, 0, 0));
	setPalette(p);
	show();
};

void PreLaunchLabel::setText(const QString &text) {
	QLabel::setText(text);
	updateGeometry();
	resize(sizeHint());
}

void PreLaunchLabel::contextMenuEvent(QContextMenuEvent *e) {
	const auto flags = textInteractionFlags();
	const auto selectable = flags
		& (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	if (!selectable) {
		e->ignore();
		return;
	}
	const auto accel = [](QKeySequence::StandardKey key) {
		return QCoreApplication::testAttribute(
				Qt::AA_DontShowShortcutsInContextMenus)
			? QString()
			: QChar('\t')
				+ QKeySequence(key).toString(QKeySequence::NativeText);
	};
	const auto menu = new QMenu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose);

	const auto copy = menu->addAction(
		u"&Copy"_q + accel(QKeySequence::Copy));
	copy->setEnabled(hasSelectedText());
	connect(copy, &QAction::triggered, this, [=] {
		if (hasSelectedText()) {
			QGuiApplication::clipboard()->setText(selectedText());
		}
	});

	menu->addSeparator();

	const auto selectAll = menu->addAction(
		u"Select All"_q + accel(QKeySequence::SelectAll));
	selectAll->setEnabled(!text().isEmpty());
	connect(selectAll, &QAction::triggered, this, [=] {
		setSelection(0, text().size());
	});

	e->accept();
	menu->popup(e->globalPos());
}

PreLaunchInput::PreLaunchInput(QWidget *parent, bool password) : QLineEdit(parent) {
	QFont logFont(font());
	logFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(logFont);

	QPalette p(palette());
	p.setColor(QPalette::Window, QColor(255, 255, 255));
	p.setColor(QPalette::Base, QColor(255, 255, 255));
	p.setColor(QPalette::WindowText, QColor(0, 0, 0));
	p.setColor(QPalette::Text, QColor(0, 0, 0));
	setPalette(p);

	QLineEdit::setTextMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);
	if (password) {
		setEchoMode(QLineEdit::Password);
	}
	show();
};

PreLaunchLog::PreLaunchLog(QWidget *parent) : QTextEdit(parent) {
	QFont logFont(font());
	logFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(logFont);

	QPalette p(palette());
	p.setColor(QPalette::WindowText, QColor(96, 96, 96));
	p.setColor(QPalette::Text, QColor(96, 96, 96));
	setPalette(p);

	setReadOnly(true);
	setFrameStyle(int(QFrame::NoFrame) | QFrame::Plain);
	viewport()->setAutoFillBackground(false);
	setContentsMargins(0, 0, 0, 0);
	document()->setDocumentMargin(0);
	show();
};

PreLaunchButton::PreLaunchButton(QWidget *parent, bool confirm) : QPushButton(parent) {
	setFlat(true);

	setObjectName(confirm ? "confirm" : "cancel");

	QFont closeFont(font());
	closeFont.setWeight(QFont::DemiBold);
	closeFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(closeFont);

	setCursor(Qt::PointingHandCursor);
	show();
};

void PreLaunchButton::setText(const QString &text) {
	QPushButton::setText(text);
	updateGeometry();
	resize(sizeHint());
}

NotStartedWindow::NotStartedWindow()
: _label(this)
, _log(this)
, _close(this) {
	_label.setText(u"Could not start AywGram Desktop!\nYou can see complete log below:"_q);

	_log.setPlainText(Logs::full());

	connect(&_close, &QPushButton::clicked, [=] { close(); });
	_close.setText(u"CLOSE"_q);

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	move(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6));
	updateControls();
	show();
}

void NotStartedWindow::updateControls() {
	_label.show();
	_log.show();
	_close.show();

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	QSize s(scr.width() / 2, scr.height() / 2);
	if (s == size()) {
		resizeEvent(0);
	} else {
		resize(s);
	}
}

void NotStartedWindow::closeEvent(QCloseEvent *e) {
	deleteLater();
	Core::Quit();
}

void NotStartedWindow::resizeEvent(QResizeEvent *e) {
	int padding = _size;
	_label.setGeometry(padding, padding, width() - 2 * padding, _label.sizeHint().height());
	_log.setGeometry(padding, padding * 2 + _label.sizeHint().height(), width() - 2 * padding, height() - 4 * padding - _label.height() - _close.height());
	_close.setGeometry(width() - padding - _close.width(), height() - padding - _close.height(), _close.width(), _close.height());
}

LastCrashedWindow::UpdaterData::UpdaterData(QWidget *buttonParent)
: check(buttonParent)
, skip(buttonParent, false) {
}

LastCrashedWindow::LastCrashedWindow(
	const QByteArray &crashdump,
	Fn<void()> launch)
: _dumpraw(crashdump)
, _label(this)
, _reportAvailable(this)
, _yourReportName(this)
, _minidump(this)
, _report(this)
, _networkSettings(this)
, _continue(this)
, _showReport(this)
, _saveReport(this)
, _getApp(this)
, _reportText(QString::fromUtf8(crashdump))
, _reportShown(false)
, _reportSaved(false)
, _reportState(crashdump.isEmpty() ? ReportNone : ReportUpdateCheck)
, _updating(this)
, _updaterData(Core::UpdaterDisabled()
	? nullptr
	: std::make_unique<UpdaterData>(this))
, _launch(std::move(launch)) {
	if (_reportState != ReportNone) {
		qint64 dumpsize = 0;
		QString dumpspath = cWorkingDir() + u"tdata/dumps"_q;
#if defined Q_OS_MAC && !defined MAC_USE_BREAKPAD
		dumpspath += u"/completed"_q;
#endif
		auto possibleDump = getReportField(qstr("Minidump:"));
		if (!possibleDump.isEmpty()) {
			if (!possibleDump.startsWith('/')) {
				possibleDump = dumpspath + '/' + possibleDump;
			}
			if (!possibleDump.endsWith(qstr(".dmp"))) {
				possibleDump += u".dmp"_q;
			}
			QFileInfo possibleInfo(possibleDump);
			if (possibleInfo.exists()) {
				_minidumpName = possibleInfo.fileName();
				dumpsize = possibleInfo.size();
			}
		}
		if (_minidumpName.isEmpty()) {
			QString maxDump;
			QDateTime maxDumpModified, workingModified = QFileInfo(cWorkingDir() + u"tdata/working"_q).lastModified();
			QFileInfoList list = QDir(dumpspath).entryInfoList();
			for (int32 i = 0, l = list.size(); i < l; ++i) {
				QString name = list.at(i).fileName();
				if (name.endsWith(qstr(".dmp"))) {
					QDateTime modified = list.at(i).lastModified();
					if (maxDump.isEmpty() || qAbs(workingModified.secsTo(modified)) < qAbs(workingModified.secsTo(maxDumpModified))) {
						maxDump = name;
						maxDumpModified = modified;
						dumpsize = list.at(i).size();
					}
				}
			}
			if (!maxDump.isEmpty() && qAbs(workingModified.secsTo(maxDumpModified)) < 10) {
				_minidumpName = maxDump;
			}
		}
		if (!_minidumpName.isEmpty()) {
			_minidump.setText(u"+ %1 (%2 KB)"_q.arg(_minidumpName).arg(dumpsize / 1024));
		}
	}

	_networkSettings.setText(u"NETWORK SETTINGS"_q);
	connect(
		&_networkSettings,
		&QPushButton::clicked,
		[=] { networkSettings(); });

	if (_reportState == ReportNone) {
		_label.setText(u"Last time AywGram Desktop was not closed properly."_q);
	} else {
		_label.setText(u"Last time AywGram Desktop crashed :("_q);
	}

	if (_updaterData) {
		_updaterData->check.setText(u"TRY AGAIN"_q);
		connect(
			&_updaterData->check,
			&QPushButton::clicked,
			[=] { updateRetry(); });
		_updaterData->skip.setText(u"SKIP"_q);
		connect(
			&_updaterData->skip,
			&QPushButton::clicked,
			[=] { updateSkip(); });

		Core::UpdateChecker checker;
		using Progress = Core::UpdateChecker::Progress;
		checker.checking(
		) | rpl::on_next([=] {
			Assert(_updaterData != nullptr);

			setUpdatingState(UpdatingCheck);
		}, _lifetime);

		checker.isLatest(
		) | rpl::on_next([=] {
			Assert(_updaterData != nullptr);

			setUpdatingState(UpdatingLatest);
		}, _lifetime);

		checker.progress(
		) | rpl::on_next([=](const Progress &result) {
			Assert(_updaterData != nullptr);

			setUpdatingState(UpdatingDownload);
			setDownloadProgress(result.already, result.size);
		}, _lifetime);

		checker.failed(
		) | rpl::on_next([=] {
			Assert(_updaterData != nullptr);

			setUpdatingState(UpdatingFail);
		}, _lifetime);

		checker.ready(
		) | rpl::on_next([=] {
			Assert(_updaterData != nullptr);

			setUpdatingState(UpdatingReady);
		}, _lifetime);

		switch (checker.state()) {
		case Core::UpdateChecker::State::Download:
			setUpdatingState(UpdatingDownload, true);
			setDownloadProgress(checker.already(), checker.size());
			break;
		case Core::UpdateChecker::State::Ready:
			setUpdatingState(UpdatingReady, true);
			break;
		default:
			setUpdatingState(UpdatingCheck, true);
			break;
		}

		cSetLastUpdateCheck(0);
		checker.start();
	} else {
		_updating.setText(u"Please check if there is a new version available."_q);
		if (_reportState != ReportNone) {
			_reportState = ReportAvailable;
		}
	}

	_reportAvailable.setText(u"A crash report is available."_q);
	_yourReportName.setText(u"Crash ID: %1"_q.arg(QString(_minidumpName).replace(".dmp", "")));
	_yourReportName.setCursor(style::cur_text);
	_yourReportName.setTextInteractionFlags(Qt::TextSelectableByMouse);

	_report.setPlainText(_reportText);

	_showReport.setText(u"VIEW REPORT"_q);
	connect(&_showReport, &QPushButton::clicked, [=] {
		_reportShown = !_reportShown;
		updateControls();
	});
	_saveReport.setText(u"SAVE TO FILE"_q);
	connect(&_saveReport, &QPushButton::clicked, [=] { saveReport(); });
	_getApp.setText(u"GET THE LATEST VERSION OF AYWGRAM DESKTOP"_q);
	connect(&_getApp, &QPushButton::clicked, [=] {
		QDesktopServices::openUrl(u"https://github.com/NahidaBuer/AywGram"_q);
	});

	_continue.setText(u"CONTINUE"_q);
	connect(&_continue, &QPushButton::clicked, [=] { processContinue(); });

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	move(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6));
	updateControls();
	show();
}

void LastCrashedWindow::saveReport() {
	QString to = QFileDialog::getSaveFileName(0, u"AywGram Crash Report"_q, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + u"/report.telegramcrash"_q, u"Telegram crash report (*.telegramcrash)"_q);
	if (!to.isEmpty()) {
		QFile file(to);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(_dumpraw);
			_reportSaved = true;
			updateControls();
		}
	}
}

QString LastCrashedWindow::getReportField(const QLatin1String &prefix) {
	QStringList lines = _reportText.split('\n');
	for (int32 i = 0, l = lines.size(); i < l; ++i) {
		if (lines.at(i).trimmed().startsWith(prefix)) {
			return lines.at(i).trimmed().mid(prefix.size()).trimmed();
		}
	}
	return QString();
}

void LastCrashedWindow::updateControls() {
	const auto padding = _size;
	const auto rowHeight = _networkSettings.height();
	auto h = padding + rowHeight + padding;

	_label.show();
	_networkSettings.hide();
	_continue.hide();
	_showReport.hide();
	_saveReport.hide();
	_getApp.hide();
	_reportAvailable.hide();
	_yourReportName.hide();
	_report.hide();
	_minidump.hide();

	auto showLocalReport = false;
	if (_updaterData) {
		_updating.show();
		_updaterData->check.hide();
		_updaterData->skip.hide();
		h += rowHeight + padding;

		const auto updateBusy = (_updaterData->state == UpdatingCheck)
			|| (_updaterData->state == UpdatingDownload);
		const auto updateFailure = (_updaterData->state == UpdatingFail)
			&& (_reportState != ReportAvailable);
		if (updateFailure) {
			_networkSettings.show();
			_updaterData->check.show();
			_updaterData->skip.show();
		} else if (updateBusy) {
			if (_updaterData->state == UpdatingCheck) {
				_networkSettings.show();
			}
			_updaterData->skip.show();
		} else {
			showLocalReport = (_reportState == ReportAvailable);
			_continue.show();
		}
	} else {
		_updating.show();
		_getApp.show();
		h += rowHeight + padding;
		h += _getApp.height() + padding;
		showLocalReport = (_reportState == ReportAvailable);
		_continue.show();
	}

	if (showLocalReport) {
		_reportAvailable.show();
		_yourReportName.show();
		h += _showReport.height() + padding;
		h += _yourReportName.height() + padding;
		if (_reportShown) {
			_report.show();
			if (!_minidumpName.isEmpty()) {
				_minidump.show();
			}
			if (!_reportSaved) {
				_saveReport.show();
			}
			h += int(_reportAvailable.height() * 12.5) + padding;
			if (!_minidumpName.isEmpty()) {
				h += _minidump.height() + padding;
			}
		} else {
			_showReport.show();
		}
	}
	h += padding + _continue.height() + padding;

	const auto s = QSize(
		2 * padding
			+ QFontMetrics(_label.font()).horizontalAdvance(
				u"Last time AywGram Desktop was not closed properly."_q)
			+ padding
			+ _networkSettings.width(),
		h);
	if (s == size()) {
		resizeEvent(0);
	} else {
		resize(s);
	}
}

void LastCrashedWindow::networkSettings() {
	const auto &proxy = Core::Sandbox::Instance().sandboxProxy();
	const auto box = new NetworkSettingsWindow(
		this,
		proxy.host,
		proxy.port ? proxy.port : kDefaultProxyPort,
		proxy.user,
		proxy.password);
	box->saveRequests(
	) | rpl::on_next([=](MTP::ProxyData &&data) {
		Assert(data.host.isEmpty() || data.port != 0);
		_proxyChanges.fire(std::move(data));
		proxyUpdated();
	}, _lifetime);
	box->show();
}

void LastCrashedWindow::proxyUpdated() {
	if (_updaterData
		&& ((_updaterData->state == UpdatingCheck)
			|| (_updaterData->state == UpdatingFail
				&& (_reportState != ReportAvailable)))) {
		Core::UpdateChecker checker;
		checker.stop();
		cSetLastUpdateCheck(0);
		checker.start();
	}
	activate();
}

rpl::producer<MTP::ProxyData> LastCrashedWindow::proxyChanges() const {
	return _proxyChanges.events();
}

void LastCrashedWindow::setUpdatingState(UpdatingState state, bool force) {
	Expects(_updaterData != nullptr);

	if (_updaterData->state != state || force) {
		_updaterData->state = state;
		switch (state) {
		case UpdatingLatest:
			_updating.setText(u"Latest version is installed."_q);
			if (_reportState == ReportNone) {
				InvokeQueued(this, [=] { processContinue(); });
			} else {
				_reportState = ReportAvailable;
			}
		break;
		case UpdatingReady:
			if (Core::checkReadyUpdate()) {
				cSetRestartingUpdate(true);
				Core::Quit();
				return;
			} else {
				setUpdatingState(UpdatingFail);
				return;
			}
		break;
		case UpdatingCheck:
			_updating.setText(u"Checking for updates..."_q);
		break;
		case UpdatingFail:
			_updating.setText(u"Update check failed :("_q);
		break;
		}
		updateControls();
	}
}

void LastCrashedWindow::setDownloadProgress(qint64 ready, qint64 total) {
	Expects(_updaterData != nullptr);

	qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
	QString readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
	QString totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
	QString res = u"Downloading update {ready} / {total} MB.."_q.replace(qstr("{ready}"), readyStr).replace(qstr("{total}"), totalStr);
	if (_updaterData->newVersionDownload != res) {
		_updaterData->newVersionDownload = res;
		_updating.setText(_updaterData->newVersionDownload);
		updateControls();
	}
}

void LastCrashedWindow::updateRetry() {
	Expects(_updaterData != nullptr);

	cSetLastUpdateCheck(0);
	Core::UpdateChecker checker;
	checker.start();
}

void LastCrashedWindow::updateSkip() {
	Expects(_updaterData != nullptr);

	if (_reportState == ReportNone) {
		processContinue();
	} else {
		if (_updaterData->state == UpdatingCheck
			|| _updaterData->state == UpdatingDownload) {
			Core::UpdateChecker checker;
			checker.stop();
			setUpdatingState(UpdatingFail);
		}
		_reportState = ReportAvailable;
		updateControls();
	}
}

void LastCrashedWindow::processContinue() {
	close();
}

void LastCrashedWindow::closeEvent(QCloseEvent *e) {
	deleteLater();

	if (CrashReports::Restart() == CrashReports::CantOpen) {
		new NotStartedWindow();
	} else {
		_launch();
	}
}

void LastCrashedWindow::resizeEvent(QResizeEvent *e) {
	const auto padding = _size;
	const auto rowHeight = _networkSettings.height();
	_label.move(padding, padding + (rowHeight - _label.height()) / 2);

	const auto updateY = padding * 2 + rowHeight;
	_updating.move(padding, updateY + (rowHeight - _updating.height()) / 2);
	_networkSettings.move(
		width() - padding - _networkSettings.width(),
		updateY);

	auto contentY = updateY + rowHeight + padding;
	if (!_updaterData) {
		_getApp.move((width() - _getApp.width()) / 2, contentY);
		contentY += _getApp.height() + padding;
	}
	_reportAvailable.move(
		padding,
		contentY + (_showReport.height() - _reportAvailable.height()) / 2);
	_showReport.move(padding * 2 + _reportAvailable.width(), contentY);
	_saveReport.move(_showReport.x(), _showReport.y());
	_yourReportName.move(
		padding,
		contentY + _showReport.height() + padding);
	_report.setGeometry(
		padding,
		_yourReportName.y() + _yourReportName.height() + padding,
		width() - 2 * padding,
		int(_reportAvailable.height() * 12.5));
	_minidump.move(padding, _report.y() + _report.height() + padding);

	_continue.move(width() - padding - _continue.width(), height() - padding - _continue.height());
	if (_updaterData) {
		_updaterData->check.move(
			width() - padding - _updaterData->check.width(),
			height() - padding - _updaterData->check.height());
		const auto checkVisible = _updaterData->check.isVisible();
		_updaterData->skip.move(
			checkVisible
				? width() - padding - _updaterData->check.width()
					- padding - _updaterData->skip.width()
				: width() - padding - _updaterData->skip.width(),
			height() - padding - _updaterData->skip.height());
	}
}

NetworkSettingsWindow::NetworkSettingsWindow(QWidget *parent, QString host, quint32 port, QString username, QString password)
: PreLaunchWindow(u"HTTP Proxy Settings"_q)
, _hostLabel(this)
, _portLabel(this)
, _usernameLabel(this)
, _passwordLabel(this)
, _hostInput(this)
, _portInput(this)
, _usernameInput(this)
, _passwordInput(this, true)
, _save(this)
, _cancel(this, false)
, _parent(parent) {
	setWindowModality(Qt::ApplicationModal);

	_hostLabel.setText(u"Hostname"_q);
	_portLabel.setText(u"Port"_q);
	_usernameLabel.setText(u"Username"_q);
	_passwordLabel.setText(u"Password"_q);

	_save.setText(u"SAVE"_q);
	connect(&_save, &QPushButton::clicked, [=] { save(); });
	_cancel.setText(u"CANCEL"_q);
	connect(&_cancel, &QPushButton::clicked, [=] { close(); });

	_hostInput.setText(host);
	_portInput.setText(QString::number(port));
	_usernameInput.setText(username);
	_passwordInput.setText(password);

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	move(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6));
	updateControls();
	show();

	_hostInput.setFocus();
	_hostInput.setCursorPosition(_hostInput.text().size());
}

void NetworkSettingsWindow::resizeEvent(QResizeEvent *e) {
	int padding = _size;
	_hostLabel.move(padding, padding);
	_hostInput.setGeometry(_hostLabel.x(), _hostLabel.y() + _hostLabel.height(), 2 * _hostLabel.width(), _hostInput.height());
	_portLabel.move(padding + _hostInput.width() + padding, padding);
	_portInput.setGeometry(_portLabel.x(), _portLabel.y() + _portLabel.height(), width() - padding - _portLabel.x(), _portInput.height());
	_usernameLabel.move(padding, _hostInput.y() + _hostInput.height() + padding);
	_usernameInput.setGeometry(_usernameLabel.x(), _usernameLabel.y() + _usernameLabel.height(), (width() - 3 * padding) / 2, _usernameInput.height());
	_passwordLabel.move(padding + _usernameInput.width() + padding, _usernameLabel.y());
	_passwordInput.setGeometry(_passwordLabel.x(), _passwordLabel.y() + _passwordLabel.height(), width() - padding - _passwordLabel.x(), _passwordInput.height());

	_save.move(width() - padding - _save.width(), height() - padding - _save.height());
	_cancel.move(_save.x() - padding - _cancel.width(), _save.y());
}

void NetworkSettingsWindow::save() {
	QString host = _hostInput.text().trimmed(), port = _portInput.text().trimmed(), username = _usernameInput.text().trimmed(), password = _passwordInput.text().trimmed();
	if (!port.isEmpty() && !port.toUInt()) {
		_portInput.setFocus();
		return;
	} else if (!host.isEmpty() && port.isEmpty()) {
		_portInput.setFocus();
		return;
	}
	_saveRequests.fire({
		.type = host.isEmpty()
			? MTP::ProxyData::Type::None
			: MTP::ProxyData::Type::Http,
		.host = host,
		.port = port.toUInt(),
		.user = username,
		.password = password,
	});
	close();
}

void NetworkSettingsWindow::closeEvent(QCloseEvent *e) {
	deleteLater();
}

rpl::producer<MTP::ProxyData> NetworkSettingsWindow::saveRequests() const {
	return _saveRequests.events();
}

void NetworkSettingsWindow::updateControls() {
	_hostInput.updateGeometry();
	_hostInput.resize(_hostInput.sizeHint());
	_portInput.updateGeometry();
	_portInput.resize(_portInput.sizeHint());
	_usernameInput.updateGeometry();
	_usernameInput.resize(_usernameInput.sizeHint());
	_passwordInput.updateGeometry();
	_passwordInput.resize(_passwordInput.sizeHint());

	int padding = _size;
	int w = 2 * padding + _hostLabel.width() * 2 + padding + _portLabel.width() * 2 + padding;
	int h = padding + _hostLabel.height() + _hostInput.height() + padding + _usernameLabel.height() + _usernameInput.height() + padding + _save.height() + padding;
	if (w == width() && h == height()) {
		resizeEvent(0);
	} else {
		setGeometry(_parent->x() + (_parent->width() - w) / 2, _parent->y() + (_parent->height() - h) / 2, w, h);
	}
}
