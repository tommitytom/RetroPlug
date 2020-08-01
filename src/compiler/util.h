#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include "logger.h"
#include "xxhash.h"

namespace fs = std::filesystem;

using u8 = unsigned char;
using u32 = unsigned int;

struct ScriptDesc {
	fs::path path;
	std::string name;
	std::string varName;
	std::vector<u8> data;

	ScriptDesc() {}
	ScriptDesc(const fs::path& _path) : path(_path) {}
};

static std::string getScriptName(fs::path path) {
	std::string name = path.replace_extension().string();
	std::replace(name.begin(), name.end(), '\\', '.');
	std::replace(name.begin(), name.end(), '/', '.');
	return name;
}

static std::string getScriptVarName(std::string name) {
	std::replace(name.begin(), name.end(), '.', '_');
	std::replace(name.begin(), name.end(), '-', '_');
	return "_" + name + "_LUA_";
}

static void parseDirectory(std::string_view dirPath, std::vector<ScriptDesc>& paths, size_t trimLeft = 0) {
	if (trimLeft == 0) {
		trimLeft = dirPath.length() + 1;
	}

	for (auto& p : fs::directory_iterator(dirPath)) {
		fs::path path = p.path();
		if (p.is_directory()) {
			parseDirectory(path.string(), paths, trimLeft);
		} else {
			if (path.extension() == ".lua") {
				ScriptDesc s = ScriptDesc(path.string().substr(trimLeft));
				s.name = getScriptName(s.path);
				s.varName = getScriptVarName(s.name);
				paths.emplace_back(s);
			}
		}
	}
}

static fs::path parsePath(const char* path) {
	fs::path configPath = path;
	configPath.make_preferred();
	if (!configPath.is_absolute()) {
		configPath = fs::current_path() / configPath;
	}

	return configPath;
}

static std::string readTextFile(const fs::path& path) {
	std::ifstream file(path);
	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void writeIfDifferent(const fs::path& path, const std::string& data) {
	std::string fileData = readTextFile(path);

	XXH64_hash_t codeHash = XXH64(data.data(), data.size(), 1337);
	XXH64_hash_t fileHash = XXH64(fileData.data(), fileData.size(), 1337);

	if (codeHash != fileHash) {
		Logger::log("Writing " + path.string());
		std::ofstream outf(path);
		outf << data;
	}
}
