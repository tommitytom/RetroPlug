#include "LoaderUtil.h"

#include <spdlog/spdlog.h>

#include "core/SystemProcessor.h"
#include "util/fs.h"

using namespace rp;

bool LoaderUtil::handleLoad(const std::vector<std::string>& files, FileManager& fileManager, Project& project) {
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
			.name = project.getName(),
			.path = path,
		});

		return true;
	} else if (romPaths.size() > 0) {
		for (size_t i = 0; i < std::min(romPaths.size(), MAX_SYSTEM_COUNT); ++i) {
			auto& pathPair = romPaths[i];
			fs::path path = pathPair.first;

			path = fileManager.addHashedFile(path, "roms");
			fs::path projectDir = fileManager.createUniqueDirectory("projects/");

			// Load system
			std::string sramPath;
			if (sramPaths.size() > 0) {
				sramPath = std::string(sramPaths[0].first);
			} else {
				sramPath = fsutil::replaceFileExt(path.string(), ".sav", false);
			}

			if (fs::exists(sramPath)) {
				// Copy .sav
				sramPath = fileManager.addUniqueFile(sramPath, projectDir).string();
			} else {
				sramPath = "";
			}

			SystemSettings systemSettings{ .romPath = path.string(), .sramPath = sramPath };
			SystemPtr system = project.addSystem(pathPair.second, systemSettings)->getSystem();
			std::string romName = system->getRomName();

			if (sramPath.empty()) {
				// TODO: Save the SRAM to a new path
			}

			// Save project
			fs::path projectPath = projectDir / "project.rplg.lua";
			spdlog::info("Saving project to {}", projectPath.string());
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
