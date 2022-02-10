#include "MenuBuilder.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "core/Project.h"
#include "sameboy/SameBoySystem.h"
#include "util/fs.h"
#include "util/SolUtil.h"
#include "util/RecentUtil.h"

using namespace rp;

#ifdef RP_WEB
const std::string_view RECENT_FILES_PATH = "/retroplug/recent.lua";
const std::string_view ROMS_PATH = "/retroplug/roms";
#else
const std::string_view RECENT_FILES_PATH = "c:/temp/retroplug/recent.lua";
const std::string_view ROMS_PATH = "c:/temp/retroplug/roms";
#endif

std::string formatRecentPath(const std::string& path) {
	std::string targetPath = fmt::format("{}/{}", ROMS_PATH, fsutil::getFilename(path));

	if (!fs::exists(targetPath)) {
		fsutil::copyFile(path, targetPath);
	}

	return targetPath;
}

void addRecent(std::string_view recentPath, RecentPath recent) {
	spdlog::debug("Adding recent path to {}", recentPath);

	recent.romPath = formatRecentPath(recent.romPath);
	recent.sramPath = formatRecentPath(recent.sramPath);
	recent.projectPath = formatRecentPath(recent.projectPath);

	try {
		sol::state s;
		SolUtil::prepareState(s);

		sol::table target;
		std::string data;

		bool valid = false;
		if (fs::exists(recentPath)) {
			data = fsutil::readTextFile(recentPath);
			spdlog::info(data);

			if (data.size() && SolUtil::deserializeTable(s, data, target)) {
				valid = true;
			}
		}

		if (!valid) {
			target = s.create_table_with("recent", s.create_table());
		}

		// This can probably be done a lot simpler than this?
		sol::protected_function_result funcRes = s.script("return require('recentUtil')");
		if (!funcRes.valid()) {
			sol::error err = funcRes;
			spdlog::error(err.what());
			return;
		}

		sol::protected_function f = funcRes.get<sol::protected_function>();
		sol::protected_function_result funcRes2 = f(target, recent.romPath, recent.romName, recent.sramPath, recent.projectPath);

		if (!funcRes2.valid()) {
			sol::error err = funcRes2;
			spdlog::error(err.what());
			return;
		}

		if (SolUtil::serializeTable(s, target, data)) {
			if (!fsutil::writeTextFile(recentPath, data)) {
				spdlog::error("Failed to write recent list to {}", recentPath);
			}
		} else {
			spdlog::error("Failed to write recent list to {}", recentPath);
		}
	} catch (...) {
		spdlog::error("Failed to update recent list");
	}
}

void loadRecent(std::string_view recentPath, std::vector<RecentPath>& paths) {
	spdlog::debug("Loading recent file list from {}", recentPath);

	if (fs::exists(recentPath)) {
		sol::state s;
		SolUtil::prepareState(s);

		std::string data = fsutil::readTextFile(recentPath);

		if (data.size()) {
			sol::table target;

			if (SolUtil::deserializeTable(s, data, target)) {
				auto entries = target["recent"].get<sol::nested<std::vector<sol::table>>>();

				for (auto& item : entries) {
					paths.push_back({
						.romPath = item["romPath"].get<std::string>(),
						.romName = item["romName"].get<std::string>(),
						.sramPath = item["sramPath"].get<std::string>(),
						.projectPath = item["projectPath"].get<std::string>()
					});

					spdlog::info("recent: {}", paths.back().romPath);
				}
			} else {
				spdlog::error("Failed to load list of recent files");
			}
		} else {
			spdlog::debug("Recent file list was empty, skipping");
		}
	} else {
		spdlog::debug("No recent file list found, skipping");
	}
}

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

const size_t MAX_SYSTEM_COUNT = 4;

bool handleSystemLoad(std::string_view romPath, std::string_view savPath, SystemPtr system) {
	std::vector<std::byte> fileData = fsutil::readFile(romPath);

	system->load({
		.romBuffer = std::make_shared<Uint8Buffer>((uint8*)fileData.data(), fileData.size()),
		.reset = true
	});

	return true;
}

bool MenuBuilder::handleLoad(const std::vector<std::string>& files, Project* project) {
	std::vector<std::string_view> projectPaths;
	std::vector<std::pair<std::string_view, SystemType>> romPaths;
	std::vector<std::pair<std::string_view, SystemType>> sramPaths;

	SystemProcessor& processor = project->getOrchestrator()->getProcessor();

	for (const std::string& path : files) {
		std::string_view ext = fsutil::getFileExt(path);

		if (ext == ".retroplug" || ext == ".rplg") {
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
		project->load(projectPaths[0]);
		addRecent(RECENT_FILES_PATH, RecentPath { .projectPath = std::string(projectPaths[0]) });

		return true;
	} else if (romPaths.size() > 0) {
		for (size_t i = 0; i < std::min(romPaths.size(), MAX_SYSTEM_COUNT); ++i) {
			auto& path = romPaths[i];

			// Load system
			std::string sramPath;
			if (sramPaths.size() > 0) {
				sramPath = std::string(sramPaths[0].first);
			} else {
				sramPath = fsutil::replaceFileExt(path.first, ".sav", false);
				if (!fs::exists(sramPath)) {
					sramPath = "";
				}
			}

			SystemPtr system = project->addSystem(path.second, path.first, sramPath);
			std::string romName = system->getRomName();

			addRecent(RECENT_FILES_PATH, RecentPath {
				.romPath = std::string(path.first),
				.romName = system->getRomName(),
				.sramPath = sramPath
			});

			/*std::string systemName = fmt::format("System {}", system->getId());

			std::shared_ptr<SystemView> view = getParent()->addChild<SystemView>(systemName);
			view->setSystem(system);

			std::vector<ViewPtr> overlays = getShared<SystemOverlayManager>()->createOverlays(system->getRomName());

			for (ViewPtr overlay : overlays) {
				overlay->setName(fmt::format("{} ({})", overlay->getName(), systemName));
				view->addChild(overlay);
			}

			this->remove();*/

			break;
		}
	}

	return false;
}

void MenuBuilder::populateRecent(Menu& root, Project* project, SystemPtr system) {
	std::vector<RecentPath> paths;
	loadRecent(RECENT_FILES_PATH, paths);

	for (const RecentPath& path : paths) {
		std::string name;

		if (!path.sramPath.empty()) {
			name = fmt::format("{} [{}]", fsutil::getFilename(path.sramPath), path.romName);
		} else if (!path.romPath.empty()) {
			name = fsutil::getFilename(path.romPath);
		}

		root.action(name, [p = path, project, system]() {
			if (system) {
				handleSystemLoad(p.romPath, p.sramPath, system);
			} else {
				handleLoad(std::vector<std::string> { p.romPath }, project);
			}
		});
	}
}

void MenuBuilder::systemAddMenu(Menu& root, Project* project, SystemPtr system) {
	Menu& loadRoot = root.subMenu("Add");

	loadRoot.action("Duplicate Current", [project, system]() { project->duplicateSystem(system->getId()); });

	populateRecent(loadRoot.subMenu("Recent"), project, nullptr);

	loadRoot
		.action("ROM...", [project]() { loadRomDialog(project, nullptr); })
		.subMenu("ROM As")
			.action("ABG...", [project]() { loadRomDialog(project, nullptr); })
			.action("CBG C...", [project]() { loadRomDialog(project, nullptr); })
			.action("DMG...", [project]() { loadRomDialog(project, nullptr); })
			.parent()
		.parent();
}

void MenuBuilder::systemLoadMenu(Menu& root, Project* project, SystemPtr system) {
	Menu& loadRoot = root.subMenu("Load");

	populateRecent(loadRoot.subMenu("Recent"), project, system);

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
