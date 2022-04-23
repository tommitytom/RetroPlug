#include "FileManager.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "util/fs.h"
#include "util/SolUtil.h"
#include "util/StlUtil.h"
#include "util/StringUtil.h"

using namespace rp;

FileManager::FileManager() {
#ifdef RP_WEB
	_rootPath = "/retroplug";
#elif RP_LINUX
	_rootPath = "~/.retroplug";
#elif RP_WINDOWS
	_rootPath = "c:\\temp\\retroplug";
#elif RP_MACOS
    _rootPath = "~/.retroplug";
#else
#error "Platform is not supported!
#endif

	_recentPath = _rootPath / "recent.lua";
}

void FileManager::addRecent(RecentFilePath&& recent) {
	spdlog::debug("Adding recent path '{}' to {}", recent.path.string(), _recentPath.string());

	try {
		sol::state s;
		SolUtil::prepareState(s);

		sol::table target;
		std::string data;

		bool valid = false;
		if (fs::exists(_recentPath)) {
			data = fsutil::readTextFile(_recentPath);

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
		sol::protected_function_result funcRes2 = f(target, recent.type, recent.name, recent.path.string());

		if (!funcRes2.valid()) {
			sol::error err = funcRes2;
			spdlog::error(err.what());
			return;
		}

		if (SolUtil::serializeTable(s, target, data)) {
			if (!fsutil::writeTextFile(_recentPath, data)) {
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
		SolUtil::prepareState(s);

		std::string data = fsutil::readTextFile(_recentPath);

		if (data.size()) {
			sol::table target;

			if (SolUtil::deserializeTable(s, data, target)) {
				auto entries = target["recent"].get<sol::nested<std::vector<sol::table>>>();

				for (auto& item : entries) {
					std::string type = item["type"].get<std::string>();

					if (types.empty() || StlUtil::vectorContains(types, type)) {
						paths.push_back({
							.type = type,
							.name = item["name"].get<std::string>(),
							.path = item["path"].get<std::string>(),
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
	uint32 contentHash = (uint32)fsutil::hashFileContent(sourceFile);
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
	fs::path fullTargetDir = _rootPath / targetDir;
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

	StringUtil::ltrim(suggested, "/\\");

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
	fs::path fullTargetDir = _rootPath / suggested.parent_path();
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
