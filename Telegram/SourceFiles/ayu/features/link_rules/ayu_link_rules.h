#pragma once

#include "ayu/libs/json.hpp"

#include <QtCore/QString>

#include <optional>

namespace Ayu::LinkRules {

enum class Source {
	Local,
	Remote,
	Bundled,
};

struct PreviewRewriteResult {
	QString originalUrl;
	QString previewUrl;
	QString ruleId;
	Source source = Source::Bundled;
	bool changed = false;
};

struct InlineQueryMatch {
	QString ruleId;
	QString username;
	uint64 botId = 0;
	QString query;
	Source source = Source::Remote;
};

[[nodiscard]] PreviewRewriteResult RewritePreviewUrl(const QString &url);
[[nodiscard]] std::optional<InlineQueryMatch> MatchInlineBot(
	const QString &text,
	bool ignoreEnableState = false);

[[nodiscard]] bool ValidateLocalSettings(const nlohmann::json &data);
[[nodiscard]] bool ApplyRemotePayloads(
	const QByteArray &pagePreview,
	const QByteArray &inlineBots,
	uint64 revision,
	QString *error = nullptr);
void Initialize();
void Invalidate();

[[nodiscard]] uint64 RemoteRevision();
[[nodiscard]] int InvalidRemoteRules();
[[nodiscard]] QString LastRemoteError();

} // namespace Ayu::LinkRules
