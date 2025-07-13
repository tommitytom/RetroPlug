#include "FileManager.h"

#include <semver/semver.hpp>
#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "foundation/OsPath.h"
#include "foundation/StlUtil.h"
#include "foundation/StringUtil.h"

#include "core/Constants.h"
#include "core/LuaUtil.h"

using namespace rp;

std::filesystem::path FileManager::getContentPath() {
	std::filesystem::path path;
	#ifdef FW_PLATFORM_WEB
		path = "/retroplug";
	#elif FW_OS_LINUX
		path = "~/.retroplug";
	#elif FW_OS_WINDOWS
		path = fw::OsPath::getContentPath();
		path /= "RetroPlug";
	#elif FW_OS_MACOS
		path = fw::OsPath::getContentPath();
		path /= "RetroPlug";
	#else
		#error "Platform is not supported!"
	#endif

	semver::version version;
	semver::parse(rp::RP_VERSION, version);
	return path / fmt::format("{}.{}", version.major(), version.minor());
}

FileManager::FileManager() {
	_rootPath = FileManager::getContentPath();
	_recentPath = _rootPath / "recent.lua";
}

Watch::Id FileManager::startWatch(const std::filesystem::path& path, Watch::Callback&& func) {
	std::string watchPath;

	if (std::filesystem::is_directory(path)) {
		watchPath = path.string();
	} else {
		watchPath = path.parent_path().string();
	}

	Watch* existing = findWatch(watchPath);
	if (!existing) {
		FW::WatchID watchId = _watcher.addWatch(watchPath, this);
		_reloaders.push_back({ watchId, watchPath });
		existing = &_reloaders.back();
	}

	existing->callbacks.push_back({ path.string(), std::move(func) });

	return existing->watchId;
}

void FileManager::handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
	spdlog::debug("File action: {} {} {}", dir, filename, action);
	std::string fullPath = (std::filesystem::path(dir) / std::filesystem::path(filename)).string();
	for (Watch& watch : _reloaders) {
		if (watch.watchId == watchid) {
			for (const auto& callback : watch.callbacks) {
				if (callback.first == fullPath || callback.first == dir) {
					callback.second(fullPath, action);
				}
			}
			return;
		}
	}
	spdlog::warn("No watch found for path: {}", fullPath);
}

void FileManager::addRecent(RecentFilePath&& recent) {
	spdlog::debug("Adding recent path '{}' to {}", recent.path.string(), _recentPath.string());

	try {
		sol::state s;
		rp::LuaUtil::prepareState(s);

		sol::table target;
		std::string data;

		bool valid = false;
		if (fs::exists(_recentPath)) {
			data = fw::FsUtil::readTextFile(_recentPath);

			if (data.size() && fw::SolUtil::deserializeTable(s, data, target)) {
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
		sol::protected_function_result funcRes2 = f(target, recent.type, recent.name, recent.path.string());

		if (!funcRes2.valid()) {
			sol::error err = funcRes2;
			spdlog::error(err.what());
			return;
		}

		if (fw::SolUtil::serializeTable(s, target, data)) {
			if (!fw::FsUtil::writeTextFile(_recentPath, data)) {
				spdlog::error("Failed to write recent list to {}", _recentPath.string());
			}
		} else {
			spdlog::error("Failed to write recent list to {}", _recentPath.string());
		}
	} catch (...) {
		spdlog::error("Failed to update recent list");
	}
}

void FileManager::loadRecent(std::vector<RecentFilePath>& paths, const std::vector<std::string>& types) {
	spdlog::debug("Loading recent file list from {}", _recentPath.string());

	if (fs::exists(_recentPath)) {
		sol::state s;
		rp::LuaUtil::prepareState(s);

		std::string data = fw::FsUtil::readTextFile(_recentPath);

		if (data.size()) {
			sol::table target;

			if (fw::SolUtil::deserializeTable(s, data, target)) {
				auto entries = target["recent"].get<sol::nested<std::vector<sol::table>>>();

				for (auto& item : entries) {
					std::string type = item["type"].get<std::string>();

					if (types.empty() || fw::StlUtil::vectorContains(types, type)) {
						std::string name = item["name"].get<std::string>();
						std::string path = item["path"].get<std::string>();

						if (name.empty()) {
							name = fs::path(path).filename().string();
						}

						paths.push_back({
							.type = type,
							.name = name,
							.path = path
						});
					}
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

fs::path FileManager::addHashedFile(const fs::path& sourceFile, const fs::path& targetDir) {
	uint32 contentHash = (uint32)fw::FsUtil::hashFileContent(sourceFile);
	std::string contentHashStr = fmt::format("{:08x}", contentHash);

	fs::path fullTargetDir = _rootPath / targetDir;
	if (!fs::exists(fullTargetDir)) {
		fs::create_directories(fullTargetDir);
	}

	for (auto& p : fs::directory_iterator(fullTargetDir)) {
		if (p.path().extension() == sourceFile.extension()) {
			std::string hashStr = p.path().filename().string().substr(0, 8);
			if (hashStr == contentHashStr) {
				// File already exists - return existing path
				return p.path();
			}
		}
	}

	// File does not already exist, add it
	fs::path targetPath = fullTargetDir / fmt::format("{}-{}", contentHashStr, sourceFile.filename().string());
	fs::copy_file(sourceFile, targetPath);

	spdlog::info("Wrote file to {}", targetPath.string());

	return targetPath;
}

fs::path FileManager::addUniqueFile(const fs::path& sourceFile, const fs::path& targetDir) {
	fs::path fullTargetDir = targetDir;
	if (!fs::exists(fullTargetDir)) {
		fs::create_directories(fullTargetDir);
	}

	fs::path fullTargetPath = getUniqueFilename(fullTargetDir / sourceFile.filename());
	fs::copy_file(sourceFile, fullTargetPath);

	return fullTargetPath;
}

fs::path FileManager::createUniqueDirectory(const std::string& suggested) {
	fs::path dirName = getUniqueDirectoryName(suggested);
	if (dirName != "") {
		fs::create_directories(dirName);
		return dirName;
	}

	return "";
}

fs::path FileManager::getUniqueDirectoryName(std::string suggested) {
	size_t countStart = 0;
	size_t countMax = 99999;

	fs::path baseDir = _rootPath.string();
	std::string dirName = "";

	fw::StringUtil::ltrim(suggested, "/\\");

	if (suggested.size() > 0) {
		baseDir = _rootPath / suggested;
		baseDir.make_preferred();

		if (baseDir.string().back() != fs::path::preferred_separator) {
			dirName = baseDir.filename().string();
			baseDir = baseDir.parent_path();

			size_t dashFound = dirName.find_first_of('-');
			if (dashFound != std::string::npos) {
				std::string beforeDash = dirName.substr(0, dashFound);
				try {
					countStart = std::stoi(beforeDash);
					countStart++;

					dirName = dirName.substr(dashFound + 1);
				} catch (...) {
					// Text before dash is not a number, ignore it
				}
			}
		}
	} else {
		baseDir.make_preferred();
	}

	fs::path fullTargetPath;

	for (size_t i = countStart; i < countMax; ++i) {
		if (dirName.size()) {
			fullTargetPath = baseDir / fmt::format("{}-{}", i, dirName);
		} else {
			fullTargetPath = baseDir / fmt::format("{}", i);
		}

		if (!fs::exists(fullTargetPath)) {
			return fullTargetPath;
		}
	}

	spdlog::error("Failed to create unique directory name!");
	return "";
}

fs::path FileManager::getUniqueFilename(const fs::path& suggested) {
	size_t countStart = 0;
	size_t countMax = 99999;
	fs::path fullTargetPath;
	fs::path fullTargetDir = suggested.parent_path();
	std::string filename = suggested.filename().string();

	size_t dashFound = filename.find_first_of('-');
	if (dashFound != std::string::npos) {
		std::string beforeDash = filename.substr(0, dashFound);
		try {
			countStart = std::stoi(beforeDash);
			countStart++;

			filename = filename.substr(dashFound + 1);
		} catch (...) {
			// Text before dash is not a number, ignore it
		}
	}

	for (size_t i = countStart; i < countMax; ++i) {
		fullTargetPath = fullTargetDir / fmt::format("{}-{}", i, filename);

		if (!fs::exists(fullTargetPath)) {
			return fullTargetPath;
		}
	}

	spdlog::error("Failed to create unique filename!");
	return "";
}
