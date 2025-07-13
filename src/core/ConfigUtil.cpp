#include "ConfigUtil.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "foundation/LuaSerializer.h"
#include "core/FileManager.h"

#include "core/LuaUtil.h"

#include <rfl/json.hpp>
#include <rfl.hpp>

namespace rp {
	void ConfigUtil::initContent(const fw::TypeRegistry& typeRegistry, RetroPlugConfig& config) {
		// Load global settings
		std::filesystem::path contentPath = FileManager::getContentPath();
		std::string configPath = (contentPath / "config.lua").string();

		if (!std::filesystem::exists(contentPath / "input")) {
			std::filesystem::create_directories(contentPath / "input");

			// TODO: look for previous config files and migrate

			const ScriptLookup& lookup = rp::CompiledScripts::config::getScriptLookup();
			for (const auto& entry : lookup) {
				if (!entry.first.starts_with("input.")) {
					continue;
				}
				std::string name = std::string(entry.first);
				char separator = std::filesystem::path::preferred_separator;
				std::replace(name.begin(), name.end(), '.', separator);
				std::filesystem::path targetPath = contentPath / (name + ".lua");
				fw::FsUtil::writeFile(targetPath, (const char*)entry.second.data, entry.second.size);
			}
		}

		std::string configData = fw::FsUtil::readTextFile(contentPath / "config.lua");
		if (configData.empty()) {
			ConfigUtil::serialize(typeRegistry, configPath, config);
		} else {
			ConfigUtil::deserializeFromMemory(typeRegistry, configData, config);
		}
	}

	std::string ConfigUtil::serialize(const fw::TypeRegistry& typeRegistry, const RetroPlugConfig& config) {
		sol::state s;
		fw::SolUtil::prepareState(s);

		sol::table projectTable = fw::LuaSerializer::serializeToObject(typeRegistry, s, config).as<sol::table>();

		std::string target;
		if (fw::SolUtil::serializeTable(s, projectTable, target)) {
			return target;
		} else {
			spdlog::error("Failed to serialize config: {}", target);
			return "{}";
		}
	}

	bool ConfigUtil::serialize(const fw::TypeRegistry& typeRegistry, std::string_view path, const RetroPlugConfig& config) {
		std::string output = serialize(typeRegistry, config);
		if (output.size()) {
			if (fw::FsUtil::writeTextFile(path, output)) {
				spdlog::info("Successfully wrote config file to {}", path);
				return true;
			} else {
				spdlog::error("Failed to save config to file");
				return false;
			}

			return true;
		}

		return false;
	}

	bool ConfigUtil::deserializeFromMemory(const fw::TypeRegistry& typeRegistry, std::string_view fileData, RetroPlugConfig& config) {
		sol::state s;
		rp::LuaUtil::prepareState(s);

		sol::table target;
		bool ok = fw::SolUtil::deserializeTable(s, fileData, target);

		if (!ok) {
			return false;
		}

		if (!fw::LuaSerializer::deserialize(typeRegistry, target, config)) {
			spdlog::error("Failed to deserialize config");
			return false;
		}

		return true;
	}

	bool ConfigUtil::deserializeFromFile(const fw::TypeRegistry& typeRegistry, std::string_view path, RetroPlugConfig& config) {
		std::string fileData = fw::FsUtil::readTextFile(path);
		if (fileData.empty()) {
			return false;
		}

		if (!ConfigUtil::deserializeFromMemory(typeRegistry, fileData, config)) {
			return false;
		}

		return true;
	}
}
