#pragma once

#include "core/SystemWrapper.h"
#include "platform/FileDialog.h"
#include "ui/Menu.h"
#include "util/RecentUtil.h"

namespace rp {
	class FileManager;
	class Project;

	const FileDialogFilter ROM_FILTER = FileDialogFilter{ "GameBoy ROM Files", "*.gb" };
	const FileDialogFilter PROJECT_FILTER = FileDialogFilter{ "RetroPlug Project Files", "*.rplg" };
	const FileDialogFilter SAV_FILTER = FileDialogFilter{ "Gameboy SAV Files", "*.sav" };
	const FileDialogFilter STATE_FILTER = FileDialogFilter{ "Gameboy State Files", "*.state" };
	const FileDialogFilter ZIP_FILTER = FileDialogFilter{ "ZIP Files", "*.zip" };
}

namespace rp::MenuBuilder {
	void populateRecent(fw::Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system);

	void systemLoadMenu(fw::Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system);

	void systemAddMenu(fw::Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system);

	void systemSaveMenu(fw::Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system);
}
