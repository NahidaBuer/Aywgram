/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

#include <QtCore/QString>

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
constexpr auto AppId = "{C9CC98BD-1BBE-4FB4-B544-13A7DD04A280}"_cs;
constexpr auto AppName = "AywGram Desktop"_cs;
constexpr auto AppFile = "AywGram"_cs;
constexpr auto AppVersion = 7001003;
constexpr auto AppVersionStr = "7.1.3";
constexpr auto AppReleaseRevision = 0;
constexpr auto AppBetaVersion = false;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;

[[nodiscard]] inline QString AppVersionString() {
	auto result = QString::fromLatin1(AppVersionStr);
	if (AppReleaseRevision > 0) {
		result += '-' + QString::number(AppReleaseRevision);
	}
	return result;
}
