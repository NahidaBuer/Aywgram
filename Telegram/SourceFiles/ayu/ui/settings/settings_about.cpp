// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_about.h"

#include "ayu/ui/settings/settings_main.h"
#include "lang_auto.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QDesktopServices>

#include "styles/style_menu_icons.h"

namespace Settings {

using namespace Builder;

namespace {

void AddExternalLink(
		SectionBuilder &builder,
		QString id,
		QString title,
		rpl::producer<QString> label,
		const style::icon *icon,
		QString url,
		QStringList altIds = {}) {
	builder.addButton({
		.id = std::move(id),
		.altIds = std::move(altIds),
		.title = rpl::single(std::move(title)),
		.icon = { icon },
		.label = std::move(label),
		.onClick = [url = std::move(url)] {
			QDesktopServices::openUrl(url);
		},
	});
}

void BuildMainDeveloper(SectionBuilder &builder) {
	builder.addSubsectionTitle(tr::ayu_AboutMainDeveloperHeader());

	AddExternalLink(
		builder,
		u"ayu/about/developer/nahida"_q,
		u"NahidaBuer"_q,
		tr::ayu_AboutDevelopmentAndMaintenance(),
		&st::menuIconProfile,
		u"https://github.com/NahidaBuer"_q,
		{ u"ayu/about"_q });
}

void BuildDirectContributors(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_AboutDirectContributorsHeader());

	AddExternalLink(
		builder,
		u"ayu/about/contributor/ireina"_q,
		u"Ireina"_q,
		tr::ayu_AboutIrenaLineContributor(),
		&st::menuIconProfile,
		u"https://github.com/re-zero001"_q);
	AddExternalLink(
		builder,
		u"ayu/about/contributor/zgx089"_q,
		u"ZGX089"_q,
		tr::ayu_AboutDirectContributor(),
		&st::menuIconProfile,
		u"https://github.com/ZG089"_q);
}

void BuildReferences(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_AboutReferencesHeader());

	AddExternalLink(
		builder,
		u"ayu/about/reference/telegram-desktop"_q,
		u"Telegram Desktop"_q,
		tr::ayu_AboutBaseProject(),
		&st::menuIconLink,
		u"https://github.com/telegramdesktop/tdesktop"_q);
	AddExternalLink(
		builder,
		u"ayu/about/reference/ayugram-desktop"_q,
		u"AyuGram Desktop"_q,
		tr::ayu_AboutBaseProject(),
		&st::menuIconLink,
		u"https://github.com/AyuGram/AyuGramDesktop"_q);
	AddExternalLink(
		builder,
		u"ayu/about/reference/yurigram"_q,
		u"Yurigram"_q,
		tr::ayu_AboutReferenceClient(),
		&st::menuIconLink,
		u"https://github.com/Revincx/Yurigram"_q);
	AddExternalLink(
		builder,
		u"ayu/about/reference/nagram"_q,
		u"Nagram"_q,
		tr::ayu_AboutReferenceClient(),
		&st::menuIconLink,
		u"https://github.com/NextAlone/Nagram"_q);
	AddExternalLink(
		builder,
		u"ayu/about/reference/nagram-ios-mithka"_q,
		u"Nagram-iOS / Mithka PR #53"_q,
		tr::ayu_AboutReferencePullRequest(),
		&st::menuIconLink,
		u"https://github.com/NextAlone/Nagram-iOS/pull/53"_q);
	AddExternalLink(
		builder,
		u"ayu/about/reference/64gram"_q,
		u"64Gram"_q,
		tr::ayu_AboutReferenceClient(),
		&st::menuIconLink,
		u"https://github.com/TDesktop-x64/tdesktop"_q);
	AddExternalLink(
		builder,
		u"ayu/about/reference/exteragram"_q,
		u"exteraGram"_q,
		tr::ayu_AboutReferenceClient(),
		&st::menuIconLink,
		u"https://github.com/exteraSquad/exteraGram"_q);

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuAbout::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_AboutTitle,
	.icon = &st::menuIconInfo,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	BuildMainDeveloper(builder);
	BuildDirectContributors(builder);
	BuildReferences(builder);
});

} // namespace

rpl::producer<QString> AyuAbout::title() {
	return tr::ayu_AboutTitle();
}

AyuAbout::AyuAbout(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuAbout::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuAboutId() {
	return AyuAbout::Id();
}

} // namespace Settings
