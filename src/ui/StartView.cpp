#include "StartView.h"

#include <sol/sol.hpp>

#include "util/fs.h"
#include "platform/FileDialog.h"
#include "core/Project.h"
#include "util/SolUtil.h"
#include "sameboy/SameBoySystem.h"

#include "roms/mgb.h"

using namespace rp;

class FileManager {
private:
	std::string _rootPath;
	std::vector<std::string> _recent;

public:
	void addRecent() {
	}

	void importFile(std::string_view path, std::string_view target) {

	}
};

void loadRecent(std::string_view recentPath, std::vector<std::string>& paths) {
	spdlog::debug("Loading recent file list from {}", recentPath);

	if (fs::exists(recentPath)) {
		sol::state s;
		SolUtil::prepareState(s);

		std::string data = fsutil::readTextFile(recentPath);

		if (data.size()) {
			sol::table target;

			if (SolUtil::deserializeTable(s, data, target)) {
				paths = target["recent"].get<sol::nested<std::vector<std::string>>>();
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

const FileDialogFilter ROM_FILTER = FileDialogFilter{ "GameBoy ROM Files", "*.gb" };
const FileDialogFilter PROJECT_FILTER = FileDialogFilter{ "RetroPlug Project Files", "*.rplg" };

const std::string_view RECENT_FILES_PATH = "/retroplug/recent.lua";
const std::string_view ROMS_PATH = "/retroplug/roms";

void addRecent(std::string_view recentPath, std::string_view pathToAdd) {
	spdlog::debug("Adding recent path to {}", recentPath);

	std::string targetPath = fmt::format("{}/{}", ROMS_PATH, fsutil::getFilename(pathToAdd));
	if (!fs::exists(targetPath)) {
		fsutil::copyFile(pathToAdd, targetPath);
	}

	pathToAdd = targetPath;

	try {
		sol::state s;
		SolUtil::prepareState(s);

		sol::table target;
		std::string data;

		bool valid = false;
		if (fs::exists(recentPath)) {
			data = fsutil::readTextFile(recentPath);

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
		sol::protected_function_result funcRes2 = f(target, pathToAdd);

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

void StartView::setupMenu() {
	spdlog::info("setting up menu");

	MenuPtr menuRoot = std::make_shared<Menu>();
	Menu& menu = *menuRoot;

	menu.title("RetroPlug v0.4.0")
		.separator()
		.action("Load...", [&](MenuContext& ctx) {
			ctx.retain();

			MenuContext* ctxPtr = &ctx;
			FileDialog::fileOpenAsync({ ROM_FILTER, PROJECT_FILTER }, true, false, [&](std::vector<std::string> items, bool valid) {
				if (valid) {
					handleLoad(items);
					ctxPtr->close();
				}
			});
		});

	std::vector<std::string> paths;
	loadRecent(RECENT_FILES_PATH, paths);

	Menu& recent = menu.subMenu("Load Recent");

	for (const std::string& path : paths) {
		recent.action(fsutil::getFilename(path), [this, p = std::string(path)]() {
			handleLoad(std::vector<std::string> { p });
		});
	}

	menu
		.action("Load MGB", [this]() {
			Project* project = getShared<Project>();
			Uint8Buffer buf(mgb, mgb_len);
			SystemPtr system = project->addSystem<SameBoySystem>(&buf);

			std::string systemName = fmt::format("System {}", system->getId());
			std::shared_ptr<SystemView> view = getParent()->addChild<SystemView>(systemName);
			view->setSystem(system);

			this->remove();
		})
		.separator()
		.subMenu("Settings")
		.parent();

	setMenu(menuRoot);
	setAutoClose(false);
}

const size_t MAX_SYSTEM_COUNT = 4;

bool StartView::handleLoad(const std::vector<std::string>& files) {
	std::vector<std::string_view> projectPaths;
	std::vector<std::pair<std::string_view, SystemType>> romPaths;
	std::vector<std::pair<std::string_view, SystemType>> sramPaths;

	Project* project = getShared<Project>();
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
		addRecent(RECENT_FILES_PATH, projectPaths[0]);

		return true;
	} else if (romPaths.size() > 0) {
		for (size_t i = 0; i < std::min(romPaths.size(), MAX_SYSTEM_COUNT); ++i) {
			auto& path = romPaths[i];

			addRecent(RECENT_FILES_PATH, path.first);

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

bool StartView::onDrop(const std::vector<std::string>& paths) {
	return handleLoad(paths);
}
