#pragma once

#include "ayu/features/aggregate/aggregate_model.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Ayu::Aggregate {

struct GlobalSettings {
	bool enabled = false;
	Parameters parameters;
	double mediaScale = 1.;
};

struct FolderSettings {
	bool visible = true;
	bool newestFirst = true;
	Mask categories = 0;
	Mask sources = 0;
	bool unmutedOnly = false;
	std::vector<std::uint64_t> excluded;
	MessageKey anchor;
	int anchorShift = 0;
	std::int64_t snapshot = 0;
	QByteArray draft;
};

[[nodiscard]] GlobalSettings ReadGlobal();
[[nodiscard]] bool FolderExists(not_null<Main::Session*> session, FilterId folder);
void WriteGlobal(GlobalSettings settings);
[[nodiscard]] FolderSettings ReadFolder(
	not_null<Main::Session*> session,
	FilterId folder);
void WriteFolder(
	not_null<Main::Session*> session,
	FilterId folder,
	const FolderSettings &settings,
	bool notify = true);
void RemoveFolder(not_null<Main::Session*> session, FilterId folder);
[[nodiscard]] rpl::producer<> SettingsChanged();
void SettingsBox(not_null<Ui::GenericBox*> box);
[[nodiscard]] QString CategoryLabel(Category category);
[[nodiscard]] QString SourceLabel(SourceType type);

} // namespace Ayu::Aggregate
