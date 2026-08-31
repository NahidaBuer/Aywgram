#pragma once

#include "core/update_metadata.h"

namespace Core {

[[nodiscard]] bool UnpackReleaseUpdate(
	const QString &filepath,
	const UpdateMetadata::Asset &asset);

} // namespace Core
