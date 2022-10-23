#pragma once

#include "core/Project.h"
#include "foundation/DataBuffer.h"

namespace rp::ProjectExporter {
	bool exportProject(Project& project, fw::Uint8Buffer& target);

	bool exportRomsAndSavs(Project& project, fw::Uint8Buffer& target);
}
