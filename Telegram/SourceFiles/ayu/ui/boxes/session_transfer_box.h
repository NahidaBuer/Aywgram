// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"

#include <memory>

namespace Main {
class Account;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

namespace Ayu::SessionTransfer {

void ShowImportBox(
	std::shared_ptr<Ui::Show> show,
	Main::Account *introAccount = nullptr,
	Fn<void()> closed = nullptr);
void ConfirmCopySession(
	std::shared_ptr<Ui::Show> show,
	not_null<Main::Account*> account);
void ConfirmExportEnvelope(
	std::shared_ptr<Ui::Show> show,
	not_null<Main::Account*> account);

} // namespace Ayu::SessionTransfer
