#include "MenuBuilder.h"

#include <unordered_set>

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "core/FileManager.h"
#include "core/Project.h"
#include "core/SystemWrapper.h"
#include "sameboy/SameBoySystem.h"
#include "util/fs.h"
#include "util/SolUtil.h"
#include "util/RecentUtil.h"

using namespace rp;

std::string formatProjectName(const std::vector<SystemWrapperPtr>& systems, const std::string& path) {
	std::string name = fsutil::getFilename(path) + " [";
	std::unordered_map<std::string, size_t> romNames;

	for (SystemWrapperPtr systemWrapper : systems) {
		SystemPtr system = systemWrapper->getSystem();

		auto found = romNames.find(system->getRomName());

		if (found != romNames.end()) {
			found->second++;
		} else {
			romNames[system->getRomName()] = 1;
		}
	}

	bool first = true;
	for (auto v : romNames) {
		if (!first) {
			name += " | ";
		}

		if (v.second == 1) {
			name += v.first;
		} else {
			name += fmt::format("{}x {}", v.second, v.first);
		}
	}

	return name + "]";
}

void loadRomDialog(Project* project, SystemWrapperPtr system) {
	std::vector<std::string> files;

	if (FileDialog::basicFileOpen(nullptr, files, { ROM_FILTER }, false)) {
		if (system) {
			LoadConfig loadConfig = LoadConfig{
				.romBuffer = std::make_shared<Uint8Buffer>(),
				.sramBuffer = std::make_shared<Uint8Buffer>()
			};

			if (!fsutil::readFile(files[0], loadConfig.romBuffer.get())) {
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
		.name = formatProjectName(project->getSystems(), path),
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

	Uint8Buffer target;
	if (system->getSystem()->saveSram(target)) {
		if (fsutil::writeFile(path, (const char*)target.data(), target.size())) {
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

	Uint8Buffer target;
	if (system->getSystem()->saveState(target)) {
		if (fsutil::writeFile(path, (const char*)target.data(), target.size())) {
			return true;
		}

		spdlog::error("Failled to write state to file");
	} else {
		spdlog::error("Failled to save state: Failed to get state from system");
	}

	return false;
}

const size_t MAX_SYSTEM_COUNT = 4;

bool handleSystemLoad(const fs::path& romPath, const fs::path& savPath, SystemWrapperPtr system) {
	std::vector<std::byte> fileData = fsutil::readFile(romPath);

	system->load({
		.romPath = romPath.string()
	}, {
		.romBuffer = std::make_shared<Uint8Buffer>((uint8*)fileData.data(), fileData.size()),
		.reset = true
	});

	return true;
}

bool MenuBuilder::handleLoad(const std::vector<std::string>& files, FileManager& fileManager, Project& project) {
	std::vector<std::string_view> projectPaths;
	std::vector<std::pair<std::string_view, SystemType>> romPaths;
	std::vector<std::pair<std::string_view, SystemType>> sramPaths;

	SystemProcessor& processor = project.getProcessor();

	for (const std::string& path : files) {
		std::string_view ext = fsutil::getFileExt(path);

		if (ext == ".retroplug" || ext == ".rplg" || ext == ".rplg.lua") {
			projectPaths.push_back(path);
		} else {
			std::vector<SystemType> loaderTypes = processor.getRomLoaders(path);
			if (loaderTypes.size()) {
				romPaths.push_back({ path, loaderTypes[0] });
			}

			loaderTypes = processor.getSramLoaders(path);
			if (loaderTypes.size()) {
				sramPaths.push_back({ path, loaderTypes[0] });
			}
		}
	}

	if (projectPaths.size() > 0) {
		// Load project
		fs::path path = fs::path(projectPaths[0]);

		
		// Copy?

		project.load(path.string());

		fileManager.addRecent(RecentFilePath{
			.type = "project",
			.name = formatProjectName(project.getSystems(), path.string()),
			.path = path,
		});

		return true;
	} else if (romPaths.size() > 0) {
		for (size_t i = 0; i < std::min(romPaths.size(), MAX_SYSTEM_COUNT); ++i) {
			auto& pathPair = romPaths[i];
			fs::path path = pathPair.first;

			path = fileManager.addHashedFile(path, "roms");

			// Load system
			std::string sramPath;
			if (sramPaths.size() > 0) {
				sramPath = std::string(sramPaths[0].first);
			} else {
				sramPath = fsutil::replaceFileExt(path.string(), ".sav", false);
			}

			if (fs::exists(sramPath)) {
				// Copy .sav
				sramPath = fileManager.addUniqueFile(sramPath, "savs").string();
			} else {
				sramPath = "";
			}

			SystemSettings systemSettings { .romPath = path.string(), .sramPath = sramPath };
			SystemPtr system = project.addSystem(pathPair.second, systemSettings)->getSystem();
			std::string romName = system->getRomName();

			if (sramPath.empty()) {
				// TODO: Save the SRAM to a new path
			}

			// Save project
			fs::path projectPath = fileManager.getUniqueFilename("projects/project.rplg.lua");
			project.save(projectPath.string());

			fileManager.addRecent(RecentFilePath{
				.type = "project",
				.name = romName,
				.path = projectPath,
			});

			break;
		}
	} else if (sramPaths.size() > 0) {

	}

	return false;
}

void MenuBuilder::populateRecent(Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system) {
	std::vector<RecentFilePath> paths;
	fileManager->loadRecent(paths);

	for (const RecentFilePath& path : paths) {
		root.action(path.name, [p = path, fileManager, project, system]() {
			if (system) {
				handleSystemLoad(p.path, "", system);
			} else {
				handleLoad(std::vector<std::string> { p.path.string() }, *fileManager, *project);
			}
		});
	}
}

void MenuBuilder::systemAddMenu(Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system) {
	Menu& loadRoot = root.subMenu("Add");

	loadRoot.action("Duplicate Current", [project, system]() { project->duplicateSystem(system->getId()); });

	populateRecent(loadRoot.subMenu("Recent"), fileManager, project, nullptr);

	loadRoot
		.action("ROM...", [project]() { loadRomDialog(project, nullptr); })
		.subMenu("ROM As")
			.action("ABG...", [project]() { loadRomDialog(project, nullptr); })
			.action("CBG C...", [project]() { loadRomDialog(project, nullptr); })
			.action("DMG...", [project]() { loadRomDialog(project, nullptr); })
			.parent()
		.parent();
}

void MenuBuilder::systemLoadMenu(Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system) {
	Menu& loadRoot = root.subMenu("Load");

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

void MenuBuilder::systemSaveMenu(Menu& root, FileManager* fileManager, Project* project, SystemWrapperPtr system) {
	root.subMenu("Save")
		.action("Project", [project, fileManager]() { saveProject(project, fileManager, false); })
		.action("Project As...", [project, fileManager]() { saveProject(project, fileManager, true); })
		.action("SAV", [project, system]() { saveSram(project, system, false); })
		.action("SAV As...", [project, system]() { saveSram(project, system, true); })
		.action("State As...", [project, system]() { saveState(project, system); })
		.action("All ROMs + SAVs", []() {})
		.parent();
}
