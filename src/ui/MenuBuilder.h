#pragma once

#include "core/SystemWrapper.h"
#include "platform/FileDialog.h"
#include "platform/Menu.h"
#include "util/RecentUtil.h"

namespace rp {
	class FileManager;
	class Project;

	const FileDialogFilter ROM_FILTER = FileDialogFilter{ "GameBoy ROM Files", "*.gb" };
	const FileDialogFilter PROJECT_FILTER = FileDialogFilter{ "RetroPlug Project Files", "*.rplg" };
	const FileDialogFilter SAV_FILTER = FileDialogFilter{ "Gameboy SAV Files", "*.sav" };
	const FileDialogFilter STATE_FILTER = FileDialogFilter{ "Gameboy State Files", "*.state" };
}

namespace rp::MenuBuilder {
	bool handleLoad(const std::vector<std::string>& files, FileManager& fileManager, Project& project);

	void populateRecent(Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system);

	void systemLoadMenu(Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system);

	void systemAddMenu(Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system);

	void systemSaveMenu(Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system);
}
