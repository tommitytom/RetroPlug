#pragma once

#include "core/System.h"
#include "ui/FileDialog.h"
#include "ui/Menu.h"
#include "util/RecentUtil.h"

namespace rp {
	class FileManager;
	class Project;

	const fw::FileDialogFilter ROM_FILTER = fw::FileDialogFilter{ "GameBoy ROM Files", "*.gb" };
	const fw::FileDialogFilter PROJECT_FILTER = fw::FileDialogFilter{ "RetroPlug Project Files", "*.rplg" };
	const fw::FileDialogFilter SAV_FILTER = fw::FileDialogFilter{ "Gameboy SAV Files", "*.sav" };
	const fw::FileDialogFilter STATE_FILTER = fw::FileDialogFilter{ "Gameboy State Files", "*.state" };
	const fw::FileDialogFilter ZIP_FILTER = fw::FileDialogFilter{ "ZIP Files", "*.zip" };
}

namespace rp::MenuBuilder {
	void populateRecent(fw::Menu& root, FileManager* fileManager, Project* project, SystemPtr system);

	void systemLoadMenu(fw::Menu& root, FileManager* fileManager, Project* project, SystemPtr system);

	void systemAddMenu(fw::Menu& root, FileManager* fileManager, Project* project, SystemPtr system);

	void systemSaveMenu(fw::Menu& root, FileManager* fileManager, Project* project, SystemPtr system);
}
