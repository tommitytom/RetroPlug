#include "MenuBuilder.h"

#include <unordered_set>

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "core/Project.h"
#include "sameboy/SameBoySystem.h"
#include "util/fs.h"
#include "util/SolUtil.h"
#include "util/RecentUtil.h"
#include "core/SystemWrapper.h"

using namespace rp;

#ifdef RP_WEB
const std::string_view RECENT_FILES_PATH = "/retroplug/recent.lua";
const std::string_view ROMS_PATH = "/retroplug/roms";
#elif RP_LINUX
const std::string_view RECENT_FILES_PATH = "~/.retroplug/recent.lua";
const std::string_view ROMS_PATH = "~/.retroplug/roms";
#elif RP_WINDOWS
const std::string_view RECENT_FILES_PATH = "c:/temp/retroplug/recent.lua";
const std::string_view ROMS_PATH = "c:/temp/retroplug/roms";
#else
#error "Platform is not supported!
#endif

std::string formatRecentPath(const std::string& path) {
	std::string targetPath = fmt::format("{}/{}", ROMS_PATH, fsutil::getFilename(path));

	if (!fs::exists(targetPath)) {
		fsutil::copyFile(path, targetPath);
	}

	return targetPath;
}

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

void addRecent(std::string_view recentPath, RecentPath recent) {
	spdlog::debug("Adding recent path to {}", recentPath);

#ifdef RP_WEB
	recent.path = formatRecentPath(recent.path);
#endif

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
		sol::protected_function_result funcRes2 = f(target, recent.path, recent.name);

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
						.path = item["path"].get<std::string>(),
						.name = item["name"].get<std::string>()
					});
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

			system->load(std::move(loadConfig));
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

	addRecent(RECENT_FILES_PATH, RecentPath {
		.path = path,
		.name = formatProjectName(project->getSystems(), path)
	});

	return project->save(path);
}

bool saveSram(Project* project, SystemWrapperPtr system, bool forceDialog) {
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

bool handleSystemLoad(std::string_view romPath, std::string_view savPath, SystemWrapperPtr system) {
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

	SystemProcessor& processor = project->getProcessor();

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
		std::string path = std::string(projectPaths[0]);
		project->load(path);

		addRecent(RECENT_FILES_PATH, RecentPath {
			.path = path,
			.name = formatProjectName(project->getSystems(), path)
		});

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

			SystemPtr system = project->addSystem(path.second, path.first, sramPath)->getSystem();
			std::string romName = system->getRomName();

			addRecent(RECENT_FILES_PATH, RecentPath {
				.path = std::string(path.first),
				.name = system->getRomName()
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

void MenuBuilder::populateRecent(Menu& root, Project* project, SystemWrapperPtr system) {
	std::vector<RecentPath> paths;
	loadRecent(RECENT_FILES_PATH, paths);

	for (const RecentPath& path : paths) {
		root.action(path.name, [p = path, project, system]() {
			if (system) {
				handleSystemLoad(p.path, "", system);
			} else {
				handleLoad(std::vector<std::string> { p.path }, project);
			}
		});
	}
}

void MenuBuilder::systemAddMenu(Menu& root, Project* project, SystemWrapperPtr system) {
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

void MenuBuilder::systemLoadMenu(Menu& root, Project* project, SystemWrapperPtr system) {
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

void MenuBuilder::systemSaveMenu(Menu& root, Project* project, SystemWrapperPtr system) {
	root.subMenu("Save")
		.action("Project", [project]() { saveProject(project, false); })
		.action("Project As...", [project]() { saveProject(project, true); })
		.action("SAV", [project, system]() { saveSram(project, system, false); })
		.action("SAV As...", [project, system]() { saveSram(project, system, true); })
		.action("State As...", [project, system]() { saveState(project, system); })
		.action("All ROMs + SAVs", []() {})
		.parent();
}
