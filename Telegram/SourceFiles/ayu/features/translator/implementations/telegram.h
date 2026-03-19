// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/features/translator/implementations/base.h"

#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>

namespace Ayu::Translator {

class TelegramTranslator final : public BaseTranslator
{
	Q_OBJECT

public:
	static TelegramTranslator &instance();

	[[nodiscard]] QSet<QString> supportedLanguages() const override { return {}; }

	[[nodiscard]] CallbackCancel startTranslation(
		const StartTranslationArgs &args
	) override;

private:
	explicit TelegramTranslator(QObject *parent = nullptr);
};

}