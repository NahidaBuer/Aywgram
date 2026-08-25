// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_session_transfer.h"

#include "ayu/ui/boxes/session_transfer_box.h"
#include "ayu/ui/settings/settings_other.h"
#include "lang_auto.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_menu_icons.h"

namespace Settings {
namespace {

using namespace Builder;

const auto kMeta = BuildHelper({
	.id = AyuSessionTransfer::Id(),
	.parentId = AyuOther::Id(),
	.title = &tr::ayu_SessionTransferTitle,
	.icon = &st::menuIconExport,
}, [](SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto session = builder.session();
	const auto show = controller->uiShow();

	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_SessionTransferExportHeader());
	builder.addButton({
		.id = u"ayu/sessionTransfer/copy"_q,
		.title = tr::ayu_SessionTransferCopy(),
		.icon = { &st::menuIconCopy },
		.onClick = [=] {
			Ayu::SessionTransfer::ConfirmCopySession(
				show,
				&session->account());
		},
	});
	builder.addButton({
		.id = u"ayu/sessionTransfer/export"_q,
		.title = tr::ayu_SessionTransferExportJson(),
		.icon = { &st::menuIconExport },
		.onClick = [=] {
			Ayu::SessionTransfer::ConfirmExportEnvelope(
				show,
				&session->account());
		},
	});
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_SessionTransferImportHeader());
	builder.addButton({
		.id = u"ayu/sessionTransfer/import"_q,
		.title = tr::ayu_SessionTransferImport(),
		.icon = { &st::menuIconImportTheme },
		.onClick = [=] {
			Ayu::SessionTransfer::ShowImportBox(show);
		},
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_SessionTransferSettingsDescription());
});

} // namespace

AyuSessionTransfer::AyuSessionTransfer(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> AyuSessionTransfer::title() {
	return tr::ayu_SessionTransferTitle();
}

void AyuSessionTransfer::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
