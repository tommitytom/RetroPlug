#pragma once

#include "core/Project.h"
#include "util/DataBuffer.h"

namespace rp::ProjectExporter {
	bool exportProject(Project& project, Uint8Buffer& target);

	bool exportRomsAndSavs(Project& project, Uint8Buffer& target);
}
