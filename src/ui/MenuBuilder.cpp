#include "MenuBuilder.h"

#include "core/Project.h"
#include "platform/FileDialog.h"
#include "sameboy/SameBoySystem.h"
#include "util/fs.h"

using namespace rp;

const FileDialogFilter ROM_FILTER = FileDialogFilter{ "GameBoy ROM Files", "*.gb" };
const FileDialogFilter PROJECT_FILTER = FileDialogFilter{ "RetroPlug Project Files", "*.rplg" };
const FileDialogFilter SAV_FILTER = FileDialogFilter{ "Gameboy SAV Files", "*.sav" };
const FileDialogFilter STATE_FILTER = FileDialogFilter{ "Gameboy State Files", "*.state" };

void loadRomDialog(Project* project, SystemPtr system) {
	std::vector<std::string> files;

	if (FileDialog::basicFileOpen(nullptr, files, { ROM_FILTER }, false)) {
		if (system) {
			//system->loadRom()
		} else {
			project->addSystem<SameBoySystem>(files[0]);
		}
	}
}

bool saveProject(Project* project, bool forceDialog) {
	std::string path;

	if (!forceDialog) {
		if (project->getState().path == "") {
			forceDialog = true;
		} else {
			path = project->getState().path;
		}
	}

	if (forceDialog) {
		if (!FileDialog::basicFileSave(nullptr, path, { PROJECT_FILTER })) {
			return false;
		}
	}

	return project->save(path);
}

bool saveSram(Project* project, SystemPtr system, bool forceDialog) {
	const SystemSettings& settings = project->getState().systemSettings[system->getId()];
	std::string path;

	if (!forceDialog) {
		if (settings.sramPath == "") {
			forceDialog = true;
		} else {
			path = settings.sramPath;
		}
	}

	if (forceDialog) {
		if (!FileDialog::basicFileSave(nullptr, path, { SAV_FILTER })) {
			return false;
		}
	}

	spdlog::info("Saving SRAM to {}", path);

	Uint8Buffer target;
	if (system->saveSram(target)) {
		if (fsutil::writeFile(path, (const char*)target.data(), target.size())) {
			return true;
		}

		spdlog::error("Failled to write SRAM to file");
	} else {
		spdlog::error("Failled to save SRAM: Failed to get state from system");
	}

	return false;
}

bool saveState(Project* project, SystemPtr system) {
	std::string path;

	if (!FileDialog::basicFileSave(nullptr, path, { STATE_FILTER })) {
		return false;
	}

	spdlog::info("Saving state to {}", path);

	Uint8Buffer target;
	if (system->saveState(target)) {
		if (fsutil::writeFile(path, (const char*)target.data(), target.size())) {
			return true;
		}

		spdlog::error("Failled to write state to file");
	} else {
		spdlog::error("Failled to save state: Failed to get state from system");
	}

	return false;
}

void MenuBuilder::systemLoadMenu(Menu& root, Project* project, SystemPtr system) {
	root.subMenu("Load")
		.subMenu("Recent")
			.parent()
		.action("Project...", []() {})
		.action("ROM...", [project]() { loadRomDialog(project, nullptr); })
		.subMenu("ROM As")
			.action("ABG...", [project]() { loadRomDialog(project, nullptr); })
			.action("CBG C...", [project]() { loadRomDialog(project, nullptr); })
			.action("DMG...", [project]() { loadRomDialog(project, nullptr); })
			.parent()
		.action("SAV...", []() {})
		.parent();
}

void MenuBuilder::systemSaveMenu(Menu& root, Project* project, SystemPtr system) {
	root.subMenu("Save")
		.action("Project", [project]() { saveProject(project, false); })
		.action("Project As...", [project]() { saveProject(project, true); })
		.action("SAV", [project, system]() { saveSram(project, system, false); })
		.action("SAV As...", [project, system]() { saveSram(project, system, true); })
		.action("State As...", [project, system]() { saveState(project, system); })
		.action("All ROMs + SAVs", []() {})
		.parent();
}
