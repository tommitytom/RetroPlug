#pragma once

#include "core/System.h"
#include "ui/FileDialogManager.h"
#include "ui/Menu.h"
#include "util/RecentUtil.h"

namespace fw::audio {
	class AudioManager;
}

namespace rp {
	class FileManager;
	class InputManager;
	class Project;
	struct GlobalSettings;
	struct RetroPlugConfig;

	const fw::FileDialogFilter ROM_FILTER = fw::FileDialogFilter{ "GameBoy ROM Files", {"*.gb"} };
	const fw::FileDialogFilter PROJECT_FILTER = fw::FileDialogFilter{ "RetroPlug Project Files", {"*.rplg"} };
	const fw::FileDialogFilter SAV_FILTER = fw::FileDialogFilter{ "Gameboy SAV Files", {"*.sav", "*.srm"} };
	const fw::FileDialogFilter STATE_FILTER = fw::FileDialogFilter{ "Gameboy State Files", {"*.state"} };
	const fw::FileDialogFilter ZIP_FILTER = fw::FileDialogFilter{ "ZIP Files", {"*.zip"} };
}

namespace rp::MenuBuilder {
	void populateRecent(fw::Menu& root, FileManager& fileManager, Project& project, SystemPtr system);

	void commonMenu(fw::Menu& root, fw::FileDialogManager& dialogManager, FileManager& fileManager, Project& project, System& system);

	void projectMenu(fw::Menu& root, FileManager& fileManager, Project& project, System& system);

	void systemMenu(fw::Menu& root, fw::FileDialogManager& dialog, FileManager& fileManager, Project& project, SystemPtr system);

	void settingsMenu(fw::Menu& root, const fw::TypeRegistry& types, InputManager& inputManager, Project& project, RetroPlugConfig& config, fw::audio::AudioManager* audioManager);

	void systemLoadMenu(fw::Menu& root, fw::FileDialogManager& dialog, FileManager& fileManager, Project& project, SystemPtr system);

	void systemAddMenu(fw::Menu& root, fw::FileDialogManager& dialog, FileManager& fileManager, Project& project, SystemPtr system);

	void systemSaveMenu(fw::Menu& root, FileManager& fileManager, Project& project, SystemPtr system);
}
