#include "MenuBuilder.h"

#include <unordered_set>

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "core/FileManager.h"
#include "core/Project.h"
#include "core/ProjectExporter.h"
#include "core/SystemWrapper.h"
#include "sameboy/SameBoySystem.h"
#include "foundation/FsUtil.h"
#include "util/LoaderUtil.h"
#include "util/RecentUtil.h"
#include "foundation/SolUtil.h"

using namespace rp;

void loadRomDialog(Project* project, SystemWrapperPtr system) {
	std::vector<std::string> files;

	if (FileDialog::basicFileOpen(nullptr, files, { ROM_FILTER }, false)) {
		if (system) {
			LoadConfig loadConfig = LoadConfig{
				.romBuffer = std::make_shared<fw::Uint8Buffer>(),
				.sramBuffer = std::make_shared<fw::Uint8Buffer>()
			};

			if (!fw::FsUtil::readFile(files[0], loadConfig.romBuffer.get())) {
				return;
			}

			//system->load(std::move(loadConfig));
		} else {
			//project->addSystem<SameBoySystem>(files[0]);
		}
	}
}

bool saveProject(Project* project, FileManager* fileManager, bool forceDialog) {
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

	fileManager->addRecent(RecentFilePath {
		.type = "project",
		.name = project->getName(),
		.path = path,
	});

	return project->save(path);
}

bool saveSram(Project* project, SystemWrapperPtr system, bool forceDialog) {
	const SystemSettings& settings = system->getSettings();
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

	fw::Uint8Buffer target;
	if (system->getSystem()->saveSram(target)) {
		if (fw::FsUtil::writeFile(path, (const char*)target.data(), target.size())) {
			return true;
		}

		spdlog::error("Failled to write SRAM to file");
	} else {
		spdlog::error("Failled to save SRAM: Failed to get state from system");
	}

	return false;
}

bool saveState(Project* project, SystemWrapperPtr system) {
	std::string path;

	if (!FileDialog::basicFileSave(nullptr, path, { STATE_FILTER })) {
		return false;
	}

	spdlog::info("Saving state to {}", path);

	fw::Uint8Buffer target;
	if (system->getSystem()->saveState(target)) {
		if (fw::FsUtil::writeFile(path, (const char*)target.data(), target.size())) {
			return true;
		}

		spdlog::error("Failled to write state to file");
	} else {
		spdlog::error("Failled to save state: Failed to get state from system");
	}

	return false;
}

bool handleSystemLoad(const fs::path& romPath, const fs::path& savPath, SystemWrapperPtr system) {
	std::vector<std::byte> fileData = fw::FsUtil::readFile(romPath);

	system->load({
		.romPath = romPath.string()
	}, {
		.romBuffer = std::make_shared<fw::Uint8Buffer>((uint8*)fileData.data(), fileData.size()),
		.reset = true
	});

	return true;
}

void MenuBuilder::populateRecent(fw::Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system) {
	std::vector<RecentFilePath> paths;
	fileManager->loadRecent(paths);

	for (const RecentFilePath& path : paths) {
		root.action(path.name, [p = path, fileManager, project, system]() {
			if (p.type == "project") {
				LoaderUtil::handleLoad(std::vector<std::string> { p.path.string() }, * fileManager, * project);
			} else {
				spdlog::error("Failed to load recent file: File type {} unknown", p.type);
			}

			/*if (system) {
				handleSystemLoad(p.path, "", system);
			} else {
				
			}*/
		});
	}
}

void MenuBuilder::systemAddMenu(fw::Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system) {
	fw::Menu& loadRoot = root.subMenu("Add");

	loadRoot.action("Duplicate Current", [fileManager, project, system]() {
		SystemSettings settings = system->getSettings();
		settings.sramPath = fileManager->getUniqueFilename(settings.sramPath).string();
		project->duplicateSystem(system->getId(), settings);
	});

	//populateRecent(loadRoot.subMenu("Recent"), fileManager, project, nullptr);

	loadRoot
		.action("ROM...", [project]() { loadRomDialog(project, nullptr); })
		.subMenu("ROM As")
			.action("ABG...", [project]() { loadRomDialog(project, nullptr); })
			.action("CBG C...", [project]() { loadRomDialog(project, nullptr); })
			.action("DMG...", [project]() { loadRomDialog(project, nullptr); })
			.parent()
		.parent();
}

void MenuBuilder::systemLoadMenu(fw::Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system) {
	fw::Menu& loadRoot = root.subMenu("Load");

	populateRecent(loadRoot.subMenu("Recent"), fileManager, project, system);

	loadRoot
		.action("Project...", []() {})
		.action("ROM...", [project, system]() { loadRomDialog(project, system); })
		.subMenu("ROM As")
			.action("ABG...", [project, system]() { loadRomDialog(project, system); })
			.action("CBG C...", [project, system]() { loadRomDialog(project, system); })
			.action("DMG...", [project, system]() { loadRomDialog(project, system); })
			.parent()
		.action("SAV...", []() {})
		.parent();
}

void MenuBuilder::systemSaveMenu(fw::Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system) {
	root.subMenu("Save")
		/*.action("Project", [project, fileManager]() { saveProject(project, fileManager, false); })
		.action("Project As...", [project, fileManager]() { saveProject(project, fileManager, true); })
		.action("SAV", [project, system]() { saveSram(project, system, false); })
		.action("SAV As...", [project, system]() { saveSram(project, system, true); })
		.action("State As...", [project, system]() { saveState(project, system); })*/
		.action("All ROMs + SAVs", [project]() {
		fw::Uint8Buffer target;
			if (ProjectExporter::exportRomsAndSavs(*project, target)) {
				FileDialog::fileSaveData(nullptr, target, { ZIP_FILTER }, project->getName() + ".zip");
			}
		})
		.parent();
}
