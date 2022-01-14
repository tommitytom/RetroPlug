#pragma once

#include "platform/Menu.h"
#include "core/Project.h"

namespace rp::MenuBuilder {
	void systemLoadMenu(Menu& root, Project* project, SystemPtr system);

	void systemSaveMenu(Menu& root, Project* project, SystemPtr system);
}
