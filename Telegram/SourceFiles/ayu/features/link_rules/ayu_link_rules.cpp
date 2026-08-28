#include "ayu/features/link_rules/ayu_link_rules.h"

#include "ayu/ayu_settings.h"
#include "base/flat_map.h"
#include "core/application.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QUrl>

#include <algorithm>
#include <memory>

namespace Ayu::LinkRules {
namespace {

constexpr auto kMaximumRules = 512;
constexpr auto kMaximumSteps = 8;
constexpr auto kMaximumPatternLength = 512;
constexpr auto kMaximumReplacementLength = 2048;
constexpr auto kMaximumUrlLength = 8 * 1024;
constexpr auto kMaximumPayloadSize = 128 * 1024;

struct Replacement {
	QRegularExpression expression;
	QString replacement;
};

struct PreviewRule {
	QString id;
	QString name;
	QString host;
	QRegularExpression hostExpression;
	bool regexHost = false;
	bool stripQuery = false;
	bool stripFragment = true;
	Source source = Source::Local;
	std::vector<Replacement> replacements;
};

struct InlineRule {
	QString id;
	QString name;
	QString username;
	uint64 botId = 0;
	Source source = Source::Local;
	std::vector<QRegularExpression> expressions;
};

struct Rules {
	std::vector<PreviewRule> previews;
	std::vector<InlineRule> inlineBots;
	uint64 remoteRevision = 0;
};

std::shared_ptr<const Rules> Current;
QByteArray RemotePagePreview;
QByteArray RemoteInlineBots;
uint64 CurrentRemoteRevision = 0;
QString RemoteError;
int CurrentInvalidRemoteRules = 0;
bool Initialized = false;
base::flat_map<QString, PreviewRewriteResult> RewriteCache;

QString CachePath() {
	return cWorkingDir() + u"tdata/ayu_remote_metadata.json"_q;
}

QString StringValue(
		const nlohmann::json &object,
		const char *key) {
	const auto i = object.find(key);
	if (i == object.end() || !i->is_string()) {
		return {};
	}
	const auto value = i->get<std::string>();
	return QString::fromUtf8(value.data(), int(value.size()));
}

bool ValidId(const QString &id) {
	static const auto expression = QRegularExpression(
		u"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$"_q);
	return expression.match(id).hasMatch();
}

bool Disabled(
		const nlohmann::json &settings,
		const char *key,
		const QString &id) {
	const auto list = settings.find(key);
	if (list == settings.end() || !list->is_array()) {
		return false;
	}
	const auto raw = id.toStdString();
	return ranges::any_of(*list, [&](const nlohmann::json &value) {
		return value.is_string() && value.get<std::string>() == raw;
	});
}

std::optional<PreviewRule> ParsePreviewRule(
		const nlohmann::json &data,
		Source source) {
	if (!data.is_object()) {
		return std::nullopt;
	}
	auto result = PreviewRule{
		.id = StringValue(data, "id"),
		.name = StringValue(data, "name"),
		.host = StringValue(data, "host"),
		.source = source,
	};
	if (!ValidId(result.id) || result.host.isEmpty()) {
		return std::nullopt;
	}
	const auto kind = StringValue(data, "host_kind");
	if (!kind.isEmpty() && kind != u"exact"_q && kind != u"regex"_q) {
		return std::nullopt;
	}
	result.regexHost = (kind == u"regex"_q);
	if (result.regexHost) {
		if (result.host.size() > kMaximumPatternLength) {
			return std::nullopt;
		}
		result.hostExpression = QRegularExpression(
			result.host,
			QRegularExpression::CaseInsensitiveOption);
		if (!result.hostExpression.isValid()) {
			return std::nullopt;
		}
	}
	const auto queryPolicy = StringValue(data, "query_policy");
	const auto fragmentPolicy = StringValue(data, "fragment_policy");
	if ((!queryPolicy.isEmpty()
			&& queryPolicy != u"preserve"_q
			&& queryPolicy != u"strip"_q)
		|| (!fragmentPolicy.isEmpty()
			&& fragmentPolicy != u"preserve"_q
			&& fragmentPolicy != u"strip"_q)) {
		return std::nullopt;
	}
	result.stripQuery = (queryPolicy == u"strip"_q);
	result.stripFragment = (fragmentPolicy != u"preserve"_q);
	const auto steps = data.find("steps");
	if (steps == data.end()
		|| !steps->is_array()
		|| steps->empty()
		|| steps->size() > kMaximumSteps) {
		return std::nullopt;
	}
	for (const auto &step : *steps) {
		const auto pattern = StringValue(step, "regex");
		const auto replacement = StringValue(step, "replace");
		if (pattern.isEmpty()
			|| pattern.size() > kMaximumPatternLength
			|| replacement.size() > kMaximumReplacementLength
			|| replacement.contains(QChar::LineFeed)
			|| replacement.contains(QChar::CarriageReturn)) {
			return std::nullopt;
		}
		auto expression = QRegularExpression(
			pattern,
			QRegularExpression::CaseInsensitiveOption);
		if (!expression.isValid()) {
			return std::nullopt;
		}
		result.replacements.push_back({
			.expression = std::move(expression),
			.replacement = replacement,
		});
	}
	return result;
}

std::optional<InlineRule> ParseInlineRule(
		const nlohmann::json &data,
		Source source) {
	if (!data.is_object()) {
		return std::nullopt;
	}
	auto result = InlineRule{
		.id = StringValue(data, "id"),
		.name = StringValue(data, "name"),
		.username = StringValue(data, "username"),
		.source = source,
	};
	try {
		result.botId = data.value("bot_id", uint64(0));
	} catch (...) {
		return std::nullopt;
	}
	static const auto username = QRegularExpression(
		u"^[A-Za-z][A-Za-z0-9_]{3,31}bot$"_q,
		QRegularExpression::CaseInsensitiveOption);
	if (!ValidId(result.id)
		|| !result.botId
		|| !username.match(result.username).hasMatch()) {
		return std::nullopt;
	}
	const auto patterns = data.find("rules");
	if (patterns == data.end()
		|| !patterns->is_array()
		|| patterns->empty()
		|| patterns->size() > kMaximumSteps) {
		return std::nullopt;
	}
	for (const auto &value : *patterns) {
		if (!value.is_string()) {
			return std::nullopt;
		}
		const auto raw = value.get<std::string>();
		const auto pattern = QString::fromUtf8(raw.data(), int(raw.size()));
		if (pattern.isEmpty() || pattern.size() > kMaximumPatternLength) {
			return std::nullopt;
		}
		auto expression = QRegularExpression(
			pattern,
			QRegularExpression::CaseInsensitiveOption);
		if (!expression.isValid()) {
			return std::nullopt;
		}
		result.expressions.push_back(std::move(expression));
	}
	return result;
}

std::vector<PreviewRule> BundledPreviewRules() {
	const auto make = [](
			QString id,
			QString name,
			QString hostPattern,
			QString replacement) {
		auto expression = QRegularExpression(
			u"^https?://"_q + hostPattern + u"(?=/|$)"_q,
			QRegularExpression::CaseInsensitiveOption);
		return PreviewRule{
			.id = std::move(id),
			.name = std::move(name),
			.host = u"^"_q + hostPattern + u"$"_q,
			.hostExpression = QRegularExpression(
				u"^"_q + hostPattern + u"$"_q,
				QRegularExpression::CaseInsensitiveOption),
			.regexHost = true,
			.source = Source::Bundled,
			.replacements = {{
				.expression = std::move(expression),
				.replacement = u"https://"_q + replacement,
			}},
		};
	};
	auto result = std::vector<PreviewRule>();
	result.push_back(make(u"twitter"_q, u"twitter.com"_q, u"twitter\\.com"_q, u"fixupx.com"_q));
	result.push_back(make(u"x"_q, u"x.com"_q, u"x\\.com"_q, u"fixupx.com"_q));
	result.push_back(make(u"tiktok"_q, u"tiktok.com"_q, u"((?:[^/]+\\.)?)tiktok\\.com"_q, u"\\1kktiktok.com"_q));
	result.push_back(make(u"reddit"_q, u"reddit.com"_q, u"(?:www\\.)?reddit\\.com"_q, u"vxreddit.com"_q));
	result.push_back(make(u"instagram"_q, u"instagram.com"_q, u"(?:www\\.)?instagram\\.com"_q, u"kkclip.com"_q));
	result.push_back(make(u"pixiv"_q, u"pixiv.net"_q, u"(?:www\\.)?pixiv\\.net"_q, u"phixiv.net"_q));
	return result;
}

nlohmann::json ParsePayload(const QByteArray &bytes) {
	if (bytes.isEmpty() || bytes.size() > kMaximumPayloadSize) {
		return nlohmann::json::object();
	}
	try {
		return nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size());
	} catch (...) {
		return nlohmann::json::object();
	}
}

template <typename Rule, typename Parser>
std::vector<Rule> ParseRuleArray(
		const nlohmann::json &payload,
		const char *key,
		Source source,
		Parser parser) {
	auto result = std::vector<Rule>();
	const auto list = payload.find(key);
	if (list == payload.end() || !list->is_array() || list->size() > kMaximumRules) {
		return result;
	}
	for (const auto &entry : *list) {
		if (!entry.is_object()) {
			continue;
		}
		const auto enabled = !entry.contains("enabled")
			|| (entry["enabled"].is_boolean()
				&& entry["enabled"].get<bool>());
		if (enabled) {
			if (auto parsed = parser(entry, source)) {
				result.push_back(std::move(*parsed));
			}
		}
	}
	return result;
}

template <typename Parser>
bool ValidateRuleArray(
		const nlohmann::json &payload,
		const char *key,
		Source source,
		Parser parser) {
	const auto list = payload.find(key);
	if (list == payload.end() || !list->is_array() || list->size() > kMaximumRules) {
		return false;
	}
	return ranges::all_of(*list, [&](const nlohmann::json &entry) {
		return entry.is_object()
			&& (!entry.contains("enabled") || entry["enabled"].is_boolean())
			&& ((entry.contains("enabled")
					&& !entry["enabled"].get<bool>())
				|| parser(entry, source).has_value());
	});
}

bool ValidateRemoteRuleShape(const nlohmann::json &entry) {
	return entry.is_object()
		&& (!entry.contains("enabled") || entry["enabled"].is_boolean());
}

nlohmann::json ObjectValue(
		const nlohmann::json &parent,
		const char *key) {
	const auto value = parent.find(key);
	return (value != parent.end() && value->is_object())
		? *value
		: nlohmann::json::object();
}

bool ValidateRemotePayloads(
		const nlohmann::json &page,
		const nlohmann::json &bots) {
	try {
		const auto pageRules = page.find("rules");
		const auto botRules = bots.find("rules");
		return page.value("schema", 0) == 1
			&& bots.value("schema", 0) == 1
			&& pageRules != page.end()
			&& botRules != bots.end()
			&& pageRules->is_array()
			&& botRules->is_array()
			&& pageRules->size() + botRules->size() <= kMaximumRules
			&& ranges::all_of(*pageRules, [](const nlohmann::json &entry) {
				return ValidateRemoteRuleShape(entry);
			})
			&& ranges::all_of(*botRules, [](const nlohmann::json &entry) {
				return ValidateRemoteRuleShape(entry);
			});
	} catch (...) {
		return false;
	}
}

int CountInvalidRemoteRules(
		const nlohmann::json &page,
		const nlohmann::json &bots) {
	auto result = 0;
	for (const auto &entry : page.at("rules")) {
		if ((!entry.contains("enabled") || entry["enabled"].get<bool>())
			&& !ParsePreviewRule(entry, Source::Remote)) {
			++result;
		}
	}
	for (const auto &entry : bots.at("rules")) {
		if ((!entry.contains("enabled") || entry["enabled"].get<bool>())
			&& !ParseInlineRule(entry, Source::Remote)) {
			++result;
		}
	}
	return result;
}

void Rebuild() {
	RewriteCache.clear();
	const auto &settings = AyuSettings::getInstance().linkRules();
	const auto localPage = ObjectValue(settings, "pagepreview");
	const auto localInline = ObjectValue(settings, "inlinebot");
	const auto remotePage = ParsePayload(RemotePagePreview);
	const auto remoteInline = ParsePayload(RemoteInlineBots);

	auto next = std::make_shared<Rules>();
	next->remoteRevision = CurrentRemoteRevision;
	next->previews = ParseRuleArray<PreviewRule>(
		localPage,
		"rules",
		Source::Local,
		ParsePreviewRule);
	auto remotePreviews = ParseRuleArray<PreviewRule>(
		remotePage,
		"rules",
		Source::Remote,
		ParsePreviewRule);
	for (auto &rule : remotePreviews) {
		if (!Disabled(settings, "disabled_remote", rule.id)) {
			next->previews.push_back(std::move(rule));
		}
	}
	auto bundled = BundledPreviewRules();
	for (auto &rule : bundled) {
		const auto shadowed = ranges::any_of(next->previews, [&](const PreviewRule &other) {
			return other.id == rule.id;
		});
		if (!shadowed && !Disabled(settings, "disabled_bundled", rule.id)) {
			next->previews.push_back(std::move(rule));
		}
	}
	next->inlineBots = ParseRuleArray<InlineRule>(
		localInline,
		"rules",
		Source::Local,
		ParseInlineRule);
	auto remoteBots = ParseRuleArray<InlineRule>(
		remoteInline,
		"rules",
		Source::Remote,
		ParseInlineRule);
	for (auto &rule : remoteBots) {
		if (!Disabled(settings, "disabled_remote", rule.id)) {
			next->inlineBots.push_back(std::move(rule));
		}
	}
	Current = std::move(next);
}

void LoadCache() {
	auto file = QFile(CachePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto root = ParsePayload(file.readAll());
	if (!root.is_object()) {
		return;
	}
	try {
		CurrentRemoteRevision = root.value("revision", uint64(0));
		const auto page = root.value("pagepreview", std::string());
		const auto bots = root.value("inlinebot", std::string());
		RemotePagePreview = QByteArray::fromBase64(
			QByteArray::fromStdString(page),
			QByteArray::Base64UrlEncoding
				| QByteArray::AbortOnBase64DecodingErrors);
		RemoteInlineBots = QByteArray::fromBase64(
			QByteArray::fromStdString(bots),
			QByteArray::Base64UrlEncoding
				| QByteArray::AbortOnBase64DecodingErrors);
		const auto pageJson = ParsePayload(RemotePagePreview);
		const auto botsJson = ParsePayload(RemoteInlineBots);
		if (!ValidateRemotePayloads(pageJson, botsJson)) {
			CurrentRemoteRevision = 0;
			CurrentInvalidRemoteRules = 0;
			RemotePagePreview.clear();
			RemoteInlineBots.clear();
			return;
		}
		CurrentInvalidRemoteRules = CountInvalidRemoteRules(
			pageJson,
			botsJson);
	} catch (...) {
		CurrentRemoteRevision = 0;
		CurrentInvalidRemoteRules = 0;
		RemotePagePreview.clear();
		RemoteInlineBots.clear();
	}
}

bool SaveCache(
		uint64 revision,
		const QByteArray &pagePreview,
		const QByteArray &inlineBots) {
	const auto root = nlohmann::json{
		{ "schema", 1 },
		{ "revision", revision },
		{ "pagepreview", pagePreview.toBase64(
			QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString() },
		{ "inlinebot", inlineBots.toBase64(
			QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString() },
	};
	const auto bytes = QByteArray::fromStdString(root.dump());
	auto file = QSaveFile(CachePath());
	return file.open(QIODevice::WriteOnly)
		&& file.write(bytes) == bytes.size()
		&& file.commit();
}

bool HostMatches(const PreviewRule &rule, const QString &host) {
	return rule.regexHost
		? rule.hostExpression.match(host).hasMatch()
		: (host.compare(rule.host, Qt::CaseInsensitive) == 0);
}

} // namespace

void Initialize() {
	if (Initialized) {
		return;
	}
	Initialized = true;
	LoadCache();
	Rebuild();
}

void Invalidate() {
	Initialize();
	Rebuild();
}

PreviewRewriteResult RewritePreviewUrl(const QString &url) {
	Initialize();
	auto result = PreviewRewriteResult{
		.originalUrl = url,
		.previewUrl = url,
	};
	if (!AyuSettings::getInstance().improveLinkPreviews()
		|| url.isEmpty()
		|| url.size() > kMaximumUrlLength) {
		return result;
	}
	const auto original = QUrl(url, QUrl::StrictMode);
	const auto scheme = original.scheme().toLower();
	if (!original.isValid()
		|| original.host().isEmpty()
		|| (scheme != u"http"_q && scheme != u"https"_q)) {
		return result;
	}
	if (const auto cached = RewriteCache.find(url);
		cached != end(RewriteCache)) {
		return cached->second;
	}
	for (const auto &rule : Current->previews) {
		if (!HostMatches(rule, original.host())) {
			continue;
		}
		auto candidateUrl = original;
		if (rule.stripQuery) {
			candidateUrl.setQuery(QString());
		}
		if (rule.stripFragment) {
			candidateUrl.setFragment(QString());
		}
		auto candidate = candidateUrl.toString(QUrl::FullyEncoded);
		for (const auto &replacement : rule.replacements) {
			candidate.replace(replacement.expression, replacement.replacement);
		}
		const auto parsed = QUrl(candidate, QUrl::StrictMode);
		if (!parsed.isValid()
			|| parsed.scheme().compare(u"https"_q, Qt::CaseInsensitive) != 0
			|| parsed.host().isEmpty()
			|| !parsed.userInfo().isEmpty()
			|| candidate.size() > kMaximumUrlLength
			|| candidate.contains(QChar::LineFeed)
			|| candidate.contains(QChar::CarriageReturn)) {
			continue;
		}
		result.previewUrl = parsed.toString(QUrl::FullyEncoded);
		result.ruleId = rule.id;
		result.source = rule.source;
		result.changed = (result.previewUrl != result.originalUrl);
		if (RewriteCache.size() >= 1024) {
			RewriteCache.clear();
		}
		RewriteCache.emplace(url, result);
		return result;
	}
	if (RewriteCache.size() >= 1024) {
		RewriteCache.clear();
	}
	RewriteCache.emplace(url, result);
	return result;
}

std::optional<InlineQueryMatch> MatchInlineBot(
		const QString &text,
		bool ignoreEnableState) {
	Initialize();
	const auto &settings = AyuSettings::getInstance();
	if ((!ignoreEnableState
			&& (!settings.autoInlineBotQueries()
				|| !settings.inlineBotConsent()))
		|| text.isEmpty()
		|| text.size() > kMaximumUrlLength) {
		return std::nullopt;
	}
	for (const auto &rule : Current->inlineBots) {
		for (const auto &expression : rule.expressions) {
			const auto match = expression.match(text);
			if (match.hasMatch()) {
				const auto query = match.captured(0);
				const auto parsed = QUrl(query, QUrl::StrictMode);
				const auto scheme = parsed.scheme().toLower();
				if (!parsed.isValid()
					|| parsed.host().isEmpty()
					|| (scheme != u"http"_q && scheme != u"https"_q)
					|| !parsed.userInfo().isEmpty()
					|| query.size() > kMaximumUrlLength
					|| query.contains(QChar::LineFeed)
					|| query.contains(QChar::CarriageReturn)) {
					continue;
				}
				return InlineQueryMatch{
					.ruleId = rule.id,
					.username = rule.username,
					.botId = rule.botId,
					.query = query,
					.source = rule.source,
				};
			}
		}
	}
	return std::nullopt;
}

bool ValidateLocalSettings(const nlohmann::json &data) {
	if (!data.is_object()) {
		return false;
	} else if (data.empty()) {
		return true;
	} else if (data.dump().size() > kMaximumPayloadSize) {
		return false;
	}
	for (const auto &[key, value] : data.items()) {
		if (key != "pagepreview"
			&& key != "inlinebot"
			&& key != "disabled_remote"
			&& key != "disabled_bundled") {
			return false;
		}
	}
	const auto page = ObjectValue(data, "pagepreview");
	const auto bots = ObjectValue(data, "inlinebot");
	const auto pageRules = page.find("rules");
	const auto botRules = bots.find("rules");
	const auto validDisabledList = [&](const char *key) {
		const auto value = data.find(key);
		return value == data.end()
			|| (value->is_array()
				&& value->size() <= kMaximumRules
				&& ranges::all_of(*value, [](const nlohmann::json &entry) {
					if (!entry.is_string()) {
						return false;
					}
					const auto raw = entry.get<std::string>();
					return ValidId(QString::fromUtf8(
						raw.data(),
						int(raw.size())));
				}));
	};
	return data.contains("pagepreview")
		&& data["pagepreview"].is_object()
		&& data.contains("inlinebot")
		&& data["inlinebot"].is_object()
		&& page.size() == 1
		&& bots.size() == 1
		&& pageRules != page.end()
		&& botRules != bots.end()
		&& pageRules->is_array()
		&& botRules->is_array()
		&& pageRules->size() + botRules->size() <= kMaximumRules
		&& validDisabledList("disabled_remote")
		&& validDisabledList("disabled_bundled")
		&& ValidateRuleArray(
			page,
			"rules",
			Source::Local,
			ParsePreviewRule)
		&& ValidateRuleArray(
			bots,
			"rules",
			Source::Local,
			ParseInlineRule);
}

bool ApplyRemotePayloads(
		const QByteArray &pagePreview,
		const QByteArray &inlineBots,
		uint64 revision,
		QString *error) {
	const auto page = ParsePayload(pagePreview);
	const auto bots = ParsePayload(inlineBots);
	const auto valid = revision && ValidateRemotePayloads(page, bots);
	if (!valid) {
		RemoteError = u"Invalid metadata payload."_q;
		if (error) {
			*error = RemoteError;
		}
		return false;
	}
	if (!SaveCache(revision, pagePreview, inlineBots)) {
		RemoteError = u"Could not save metadata cache."_q;
		if (error) {
			*error = RemoteError;
		}
		return false;
	}
	RemotePagePreview = pagePreview;
	RemoteInlineBots = inlineBots;
	CurrentRemoteRevision = revision;
	CurrentInvalidRemoteRules = CountInvalidRemoteRules(page, bots);
	RemoteError.clear();
	Rebuild();
	return true;
}

uint64 RemoteRevision() {
	Initialize();
	return CurrentRemoteRevision;
}

int InvalidRemoteRules() {
	Initialize();
	return CurrentInvalidRemoteRules;
}

QString LastRemoteError() {
	return RemoteError;
}

} // namespace Ayu::LinkRules
