// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtNetwork/QNetworkReply>
#include <QJsonDocument>

class AyuLanguage : public QObject
{
	Q_OBJECT
	Q_DISABLE_COPY(AyuLanguage)

public:
	static AyuLanguage *currentInstance();
	static void init();
	static AyuLanguage *instance;

	void fetchLanguage(const QString &id, const QString &baseId);
	[[nodiscard]] bool applyLanguagePack(
		const QJsonDocument &doc,
		const QString &expectedLocale);

public Q_SLOTS:
	void fetchFinished();

private:
	enum class RequestKind {
		None,
		Manifest,
		LanguagePack,
	};

	AyuLanguage();
	~AyuLanguage() override = default;

	void loadCachedLanguage();
	void loadBundledLanguage();
	[[nodiscard]] bool applyBundledZhHans();
	[[nodiscard]] bool applyLegacyLanguageJson(const QJsonDocument &doc);
	void requestManifest();
	void requestLanguagePack(
		const QString &locale,
		const QString &path,
		const QByteArray &sha256,
		qint64 size);
	void saveCachedLanguage(const QByteArray &json, const QString &langId);
	[[nodiscard]] QString getCacheDir() const;
	[[nodiscard]] QString getCachePath(const QString &langId) const;
	[[nodiscard]] QString normalizedLocale(const QString &id) const;

	QNetworkAccessManager networkManager;
	QNetworkReply *_reply = nullptr;
	RequestKind _requestKind = RequestKind::None;
	QString _currentLangId;
	QString _baseLangId;
	QByteArray _expectedHash;
	qint64 _expectedSize = 0;

};
