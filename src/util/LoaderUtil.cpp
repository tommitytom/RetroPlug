#include "LoaderUtil.h"

#include <spdlog/spdlog.h>

#include "core/ProjectSerializer.h"
#include "core/SystemProcessor.h"
#include "foundation/FsUtil.h"

using namespace rp;

bool LoaderUtil::handleLoad(const std::vector<std::string>& files, FileManager& fileManager, Project& project) {
	std::vector<std::string_view> projectPaths;
	std::vector<std::pair<std::string_view, SystemType>> romPaths;
	std::vector<std::pair<std::string_view, SystemType>> sramPaths;

	const SystemFactory& factory = project.getSystemFactory();

	for (const std::string& path : files) {
		std::string_view ext = fw::FsUtil::getFileExt(path);

		if (ext == ".retroplug" || ext == ".rplg" || ext == ".rplg.lua") {
			projectPaths.push_back(path);
		} else {
			std::vector<SystemType> loaderTypes = factory.getRomLoaders(path);
			if (loaderTypes.size()) {
				romPaths.push_back({ path, loaderTypes[0] });
			}

			loaderTypes = factory.getSramLoaders(path);
			if (loaderTypes.size()) {
				sramPaths.push_back({ path, loaderTypes[0] });
			}
		}
	}

	if (projectPaths.size() > 0) {
		// Load project
		std::string path = std::string(projectPaths[0]);

		if (project.load(path)) {
			fileManager.addRecent(RecentFilePath{
				.type = "project",
				.name = project.getName(),
				.path = path,
			});

			return true;
		}
	} else if (romPaths.size() == 1) {
		const auto& pathPair = romPaths[0];
		std::string romPath = std::string(pathPair.first);
		std::string sramPath;
		if (sramPaths.size() > 0) {
			sramPath = std::string(sramPaths[0].first);
		} else {
			sramPath = fw::FsUtil::replaceFileExt(romPath, ".sav", false);
		}
		if (!fs::exists(sramPath)) {
			sramPath = "";
		} else {
			const std::string projectPath = fw::FsUtil::replaceFileExt(sramPath, ".rplg", false);
			// Is there a project matching save/rom path?

			if (fw::FsUtil::exists(projectPath)) {
				ProjectState projectState;
				std::vector<SystemDesc> systemDescs;
				const fw::TypeRegistry& t = project.getTypeRegistry();

				if (ProjectSerializer::deserializeFromFile(t, projectPath, projectState, systemDescs)) {
					for (const SystemDesc& desc : systemDescs) {
						if (desc.paths.sramPath == sramPath && desc.paths.romPath == romPath) {
							spdlog::info("Found matching project for save path: {}", projectPath);
							if (project.load(projectPath)) {
								fileManager.addRecent(RecentFilePath{
									.type = "project",
									.name = project.getName(),
									.path = projectPath,
								});

								return true;
							}
						}
					}
				}
			}
		}

		project.clear();

		SystemDesc desc{
			.paths = { .romPath = romPath, .sramPath = sramPath },
			.settings = project.getGlobalConfig().system
		};
		SystemPtr system = project.addSystem(pathPair.second, desc);
		if (system) {
			std::string romName = system->getRomName();
			std::string romFileName = fw::FsUtil::getFilename(romPath);
			if (romName.size() && romFileName.size()) {
				romName += " (" + romFileName + ")";
			} else if (romName.empty() && romFileName.size()) {
				romName = romFileName;
			}
			fileManager.addRecent(RecentFilePath{
				.type = "rom",
				.name = romName,
				.path = romPath,
			});
			return true;
		} else {
			spdlog::error("Failed to add system for ROM: {}", romPath);
			return false;
		}
	} else if (romPaths.size() > 1) {
		bool valid = false;

		project.clear();

		for (size_t i = 0; i < std::min(romPaths.size(), MAX_SYSTEM_COUNT); ++i) {
			const auto& pathPair = romPaths[i];
			std::string romPath = std::string(pathPair.first);

			std::string sramPath;
			if (sramPaths.size() > i) {
				sramPath = std::string(sramPaths[i].first);
			} else {
				sramPath = fw::FsUtil::replaceFileExt(romPath, ".sav", false);
			}

			if (!fs::exists(sramPath)) {
				sramPath = "";
			}

			SystemDesc desc{
				.paths = { .romPath = romPath, .sramPath = sramPath },
				.settings = project.getGlobalConfig().system
			};

			SystemPtr system = project.addSystem(pathPair.second, desc);
			if (system) {
				valid = true;
				std::string romName = system->getRomName();
				std::string romFileName = fw::FsUtil::getFilename(romPath);

				if (romName.size() && romFileName.size()) {
					romName += " (" + romFileName + ")";
				} else if (romName.empty() && romFileName.size()) {
					romName = romFileName;
				}

				fileManager.addRecent(RecentFilePath{
					.type = "rom",
					.name = romName,
					.path = romPath,
				});
			} else {
				spdlog::error("Failed to add system for ROM: {}", romPath);
			}
		}

		return true;

		/*for (size_t i = 0; i < std::min(romPaths.size(), MAX_SYSTEM_COUNT); ++i) {
			auto& pathPair = romPaths[i];
			fs::path path = pathPair.first;

			fs::path hashedRomPath = fileManager.addHashedFile(path, "roms");
			fs::path projectDir = fileManager.createUniqueDirectory("projects/");

			// Load system
			std::string sramPath;
			if (sramPaths.size() > 0) {
				sramPath = std::string(sramPaths[0].first);
			} else {
				sramPath = fw::FsUtil::replaceFileExt(path.string(), ".sav", false);
			}

			if (fs::exists(sramPath)) {
				// Copy .sav
				sramPath = fileManager.addUniqueFile(sramPath, projectDir).string();
			} else {
				sramPath = "";
			}

			SystemDesc desc{
				.paths = { .romPath = hashedRomPath.string(), .sramPath = sramPath },
				.settings = project.getGlobalConfig().system
			};

			SystemPtr system = project.addSystem(pathPair.second, desc);
			std::string romName = system->getRomName();

			// Save project
			fs::path projectPath = projectDir / "project.rplg.lua";
			spdlog::info("Saving project to {}", projectPath.string());
			project.save(projectPath.string());

			fileManager.addRecent(RecentFilePath{
				.type = "project",
				.name = project.getName(),
				.path = projectPath.string(),
			});

			break;
		}*/
	}

	return false;
}
