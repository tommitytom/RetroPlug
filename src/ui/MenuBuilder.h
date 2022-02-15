#pragma once

#include "platform/Menu.h"
#include "core/Project.h"
#include "util/RecentUtil.h"

#include "platform/FileDialog.h"

namespace rp {
	const FileDialogFilter ROM_FILTER = FileDialogFilter{ "GameBoy ROM Files", "*.gb" };
	const FileDialogFilter PROJECT_FILTER = FileDialogFilter{ "RetroPlug Project Files", "*.rplg" };
	const FileDialogFilter SAV_FILTER = FileDialogFilter{ "Gameboy SAV Files", "*.sav" };
	const FileDialogFilter STATE_FILTER = FileDialogFilter{ "Gameboy State Files", "*.state" };
}

namespace rp::MenuBuilder {
	bool handleLoad(const std::vector<std::string>& files, Project* project);

	void populateRecent(Menu& root, Project* project, SystemWrapperPtr system);

	void systemLoadMenu(Menu& root, Project* project, SystemWrapperPtr system);

	void systemAddMenu(Menu& root, Project* project, SystemWrapperPtr system);

	void systemSaveMenu(Menu& root, Project* project, SystemWrapperPtr system);
}
