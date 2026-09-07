// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_lang.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_instance.h"
#include "storage/localstorage.h"
#include "lang_auto_counts.h"

#include <QtNetwork/QNetworkRequest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <map>

namespace {

constexpr auto kManifestUrl =
	"https://cdn.jsdelivr.net/gh/AywGram/LanguagePacks@main/dist/manifest.json";
constexpr auto kDistUrl =
	"https://cdn.jsdelivr.net/gh/AywGram/LanguagePacks@main/dist/";
constexpr auto kMaximumManifestSize = 1024 * 1024;
constexpr auto kMaximumPackSize = 4 * 1024 * 1024;

const auto kLanguageMapping = std::map<QString, QString>{
	{ u"pt-br"_q, u"pt"_q },
	{ u"zh-hans-beta"_q, u"zh-hans"_q },
	{ u"zh-hant-beta"_q, u"zh-hant"_q },
	{ u"zh-hans-raw"_q, u"zh-hans"_q },
	{ u"zh-hant-raw"_q, u"zh-hant"_q },
};

[[nodiscard]] bool IsPlaceholderName(const QString &value) {
	if (value.isEmpty() || !value.front().isLetter()) {
		return false;
	}
	for (const auto character : value) {
		if (!character.isLetterOrNumber() && character != u'_') {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QSet<QString> Placeholders(const QString &value) {
	auto result = QSet<QString>();
	for (auto offset = 0; offset < value.size();) {
		const auto begin = value.indexOf(u'{', offset);
		if (begin < 0) {
			break;
		}
		const auto end = value.indexOf(u'}', begin + 1);
		if (end < 0) {
			break;
		}
		const auto name = value.mid(begin + 1, end - begin - 1);
		if (IsPlaceholderName(name)) {
			result.insert(name);
		}
		offset = end + 1;
	}
	return result;
}

[[nodiscard]] QByteArray Sha256(const QByteArray &data) {
	return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

} // namespace

AyuLanguage *AyuLanguage::instance = nullptr;

AyuLanguage::AyuLanguage() = default;

void AyuLanguage::init() {
	if (!instance) {
		instance = new AyuLanguage;
	}
	instance->loadBundledLanguage();
	instance->loadCachedLanguage();
}

AyuLanguage *AyuLanguage::currentInstance() {
	return instance;
}

QString AyuLanguage::normalizedLocale(const QString &id) const {
	auto result = id.toLower().replace(u'_', u'-');
	const auto i = kLanguageMapping.find(result);
	if (i != end(kLanguageMapping)) {
		result = i->second;
	}
	return result;
}

QString AyuLanguage::getCacheDir() const {
	return cWorkingDir() + u"tdata/ayu/languages/v1/"_q;
}

QString AyuLanguage::getCachePath(const QString &langId) const {
	return getCacheDir() + langId + u".json"_q;
}

bool AyuLanguage::applyBundledZhHans() {
	QFile file(u":/gui/langs/zh-hans.lproj/zh-hans.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	QJsonParseError error{};
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError) {
		return false;
	}
	return applyLegacyLanguageJson(document);
}

void AyuLanguage::loadBundledLanguage() {
	const auto current = normalizedLocale(Lang::GetInstance().id());
	const auto base = normalizedLocale(Lang::GetInstance().baseId());
	if (current == u"zh-hans"_q || base == u"zh-hans"_q) {
		if (applyBundledZhHans()) {
			LOG(("Loaded canonical bundled AywGram zh-hans language."));
		}
	}

	const auto load = [&](const QString &locale) {
		if (locale.isEmpty()) {
			return false;
		}
		QFile file(u":/gui/ayw_langpacks/locales/"_q + locale + u".json"_q);
		if (!file.open(QIODevice::ReadOnly)) {
			return false;
		}
		QJsonParseError error{};
		const auto document = QJsonDocument::fromJson(file.readAll(), &error);
		return (error.error == QJsonParseError::NoError)
			&& applyLanguagePack(document, locale);
	};
	if (!load(current) && base != current) {
		load(base);
	}
}

void AyuLanguage::loadCachedLanguage() {
	const auto current = normalizedLocale(Lang::GetInstance().id());
	const auto base = normalizedLocale(Lang::GetInstance().baseId());
	const auto load = [&](const QString &locale) {
		if (locale.isEmpty()) {
			return false;
		}
		QFile file(getCachePath(locale));
		if (!file.open(QIODevice::ReadOnly)) {
			return false;
		}
		QJsonParseError error{};
		const auto document = QJsonDocument::fromJson(file.readAll(), &error);
		if (error.error != QJsonParseError::NoError
			|| !applyLanguagePack(document, locale)) {
			return false;
		}
		LOG(("Loaded cached AywGram language: %1").arg(locale));
		return true;
	};
	if (!load(current) && base != current) {
		load(base);
	}
}

void AyuLanguage::saveCachedLanguage(
		const QByteArray &json,
		const QString &langId) {
	const auto cacheDir = getCacheDir();
	if (!QDir().mkpath(cacheDir)) {
		return;
	}
	QSaveFile file(getCachePath(langId));
	if (file.open(QIODevice::WriteOnly)
		&& file.write(json) == json.size()
		&& file.commit()) {
		LOG(("Cached AywGram language atomically: %1").arg(langId));
	}
}

void AyuLanguage::fetchLanguage(const QString &id, const QString &baseId) {
	_currentLangId = normalizedLocale(id);
	_baseLangId = normalizedLocale(baseId);
	if (_currentLangId.isEmpty()) {
		_currentLangId = _baseLangId;
	}
	if (_currentLangId.isEmpty()) {
		return;
	}

	if (Core::App().settings().proxy().isEnabled()) {
		const auto proxy = Core::App().settings().proxy().selected();
		if (proxy.type == MTP::ProxyData::Type::Socks5
			|| proxy.type == MTP::ProxyData::Type::Http) {
			networkManager.setProxy(ToNetworkProxy(ToDirectIpProxy(proxy)));
		}
	}
	requestManifest();
}

void AyuLanguage::requestManifest() {
	if (_reply) {
		_reply->disconnect(this);
		_reply->abort();
		_reply->deleteLater();
	}
	_requestKind = RequestKind::Manifest;
	_reply = networkManager.get(QNetworkRequest(
		QUrl(QString::fromLatin1(kManifestUrl))));
	connect(_reply, &QNetworkReply::finished, this, &AyuLanguage::fetchFinished);
}

void AyuLanguage::requestLanguagePack(
		const QString &locale,
		const QString &path,
		const QByteArray &sha256,
		qint64 size) {
	_currentLangId = locale;
	_expectedHash = sha256;
	_expectedSize = size;
	_requestKind = RequestKind::LanguagePack;
	_reply = networkManager.get(QNetworkRequest(
		QUrl(QString::fromLatin1(kDistUrl) + path)));
	connect(_reply, &QNetworkReply::finished, this, &AyuLanguage::fetchFinished);
}

void AyuLanguage::fetchFinished() {
	if (!_reply) {
		return;
	}
	const auto reply = _reply;
	const auto kind = _requestKind;
	_reply = nullptr;
	_requestKind = RequestKind::None;
	const auto result = reply->readAll();
	const auto error = reply->error();
	reply->deleteLater();
	if (error != QNetworkReply::NoError) {
		LOG(("Could not update AywGram language: network error %1.").arg(error));
		return;
	}

	if (kind == RequestKind::Manifest) {
		if (result.size() > kMaximumManifestSize) {
			return;
		}
		QJsonParseError parseError{};
		const auto manifest = QJsonDocument::fromJson(result, &parseError).object();
		if (parseError.error != QJsonParseError::NoError
			|| manifest.value(u"schema"_q).toInt() != 1) {
			return;
		}
		const auto locales = manifest.value(u"locales"_q).toObject();
		auto locale = _currentLangId;
		auto entry = locales.value(locale).toObject();
		if (entry.isEmpty() && !_baseLangId.isEmpty() && _baseLangId != locale) {
			locale = _baseLangId;
			entry = locales.value(locale).toObject();
		}
		const auto path = entry.value(u"path"_q).toString();
		const auto hash = entry.value(u"sha256"_q).toString().toLatin1();
		const auto size = entry.value(u"size"_q).toInteger();
		if (entry.isEmpty()
			|| !path.startsWith(u"locales/"_q)
			|| path.contains(u".."_q)
			|| hash.size() != 64
			|| size <= 0
			|| size > kMaximumPackSize) {
			return;
		}
		requestLanguagePack(locale, path, hash, size);
		return;
	}

	if (kind == RequestKind::LanguagePack) {
		if (result.size() != _expectedSize || Sha256(result) != _expectedHash) {
			LOG(("Rejected AywGram language with mismatched size or SHA-256."));
			return;
		}
		QJsonParseError parseError{};
		const auto document = QJsonDocument::fromJson(result, &parseError);
		if (parseError.error == QJsonParseError::NoError
			&& applyLanguagePack(document, _currentLangId)) {
			saveCachedLanguage(result, _currentLangId);
		}
	}
}

bool AyuLanguage::applyLanguagePack(
		const QJsonDocument &doc,
		const QString &expectedLocale) {
	const auto root = doc.object();
	if (root.value(u"schema"_q).toInt() != 1
		|| root.value(u"locale"_q).toString() != expectedLocale
		|| !root.value(u"strings"_q).isObject()) {
		return false;
	}
	const auto strings = root.value(u"strings"_q).toObject();
	auto applied = 0;
	for (auto i = strings.constBegin(); i != strings.constEnd(); ++i) {
		const auto rawKey = i.key();
		const auto entry = i.value().toObject();
		const auto value = entry.value(u"value"_q).toString();
		const auto fingerprint = entry.value(u"source"_q).toString().toLatin1();
		if (rawKey.isEmpty()
			|| value.trimmed().isEmpty()
			|| fingerprint.size() != 64) {
			continue;
		}
		const auto key = (u"ayu_"_q + rawKey).toUtf8();
		const auto index = Lang::GetKeyIndex(QLatin1String(key));
		if (index == Lang::kKeysCount) {
			continue;
		}
		const auto source = Lang::GetOriginalValue(index);
		if (Sha256(source.toUtf8()) != fingerprint
			|| Placeholders(source) != Placeholders(value)) {
			continue;
		}
		Lang::GetInstance().resetValue(key);
		Lang::GetInstance().applyValue(key, value.toUtf8());
		++applied;
	}
	if (applied) {
		Lang::GetInstance().updatePluralRules();
	}
	return true;
}

bool AyuLanguage::applyLegacyLanguageJson(const QJsonDocument &doc) {
	const auto json = doc.object();
	for (auto i = json.constBegin(); i != json.constEnd(); ++i) {
		if (!i.value().isString()) {
			continue;
		}
		const auto key = (u"ayu_"_q + i.key()).toUtf8();
		if (Lang::GetKeyIndex(QLatin1String(key)) == Lang::kKeysCount) {
			continue;
		}
		Lang::GetInstance().resetValue(key);
		Lang::GetInstance().applyValue(key, i.value().toString().toUtf8());
	}
	Lang::GetInstance().updatePluralRules();
	return true;
}
