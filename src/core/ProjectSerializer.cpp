#include "ProjectSerializer.h"

#include <sol/sol.hpp>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "foundation/MetaUtil.h"

#include "core/LuaUtil.h"
#include "foundation/LuaSerializer.h"

using namespace rp;

const std::string_view PROJECT_VERSION = "1.0.0";
const std::string_view RP_VERSION = "0.4.0";

std::string ProjectSerializer::serialize(const fw::TypeRegistry& typeRegistry, const ProjectState& state, const std::vector<SystemDesc>& systems) {
	sol::state s;
	fw::SolUtil::prepareState(s);

	sol::table projectTable = fw::LuaSerializer::serializeToObject(typeRegistry, s, state).as<sol::table>();
	projectTable["projectVersion"] = PROJECT_VERSION;
	projectTable["retroPlugVersion"] = RP_VERSION;

	projectTable["systems"] = fw::LuaSerializer::serializeToObject(typeRegistry, s, systems);

	std::string target;
	if (fw::SolUtil::serializeTable(s, projectTable, target)) {
		return target;
	} else {
		spdlog::error("Failed to serialize project: {}", target);
		return "{}";
	}
}

bool ProjectSerializer::serialize(const fw::TypeRegistry& typeRegistry, std::string_view path, ProjectState& state, const std::vector<SystemDesc>& systems, bool updatePath) {
	if (updatePath) {
		state.path = std::string(path.data());
	}
	
	std::string output = serialize(typeRegistry, state, systems);
	if (output.size()) {
		if (fw::FsUtil::writeTextFile(path, output)) {
			spdlog::info("Successfully wrote project file to {}", path);
			return true;
		} else {
			spdlog::error("Failed to save project to file");
			return false;
		}

		return true;
	}

	return false;
}

bool ProjectSerializer::deserializeFromMemory(const fw::TypeRegistry& typeRegistry, std::string_view fileData, ProjectState& state, std::vector<SystemDesc>& systemSettings) {
	sol::state s;
	rp::LuaUtil::prepareState(s);

	sol::table target;
	bool ok = fw::SolUtil::deserializeTable(s, fileData, target);

	if (!ok) {
		return false;
	}

	state = ProjectState();

	std::string projectVersion = target["projectVersion"];
	std::string retroPlugVersion = target["retroPlugVersion"];

	// TODO: Check versions - migrate if necessary

	sol::table settings = target["settings"];
	if (!fw::LuaSerializer::deserialize(typeRegistry, settings, state.settings)) {
		spdlog::error("Failed to deserialize project settings");
		//return false;
	}

	sol::table systemsTable = target["systems"];
	if (!fw::LuaSerializer::deserialize(typeRegistry, systemsTable, systemSettings)) {
		spdlog::error("Failed to deserialize systems");
		//return false;
	}

	return true;
}

bool ProjectSerializer::deserializeFromFile(const fw::TypeRegistry& typeRegistry, std::string_view path, ProjectState& state, std::vector<SystemDesc>& systemSettings) {
	std::string fileData = fw::FsUtil::readTextFile(path);
	if (fileData.empty()) {
		return false;
	}

	if (!ProjectSerializer::deserializeFromMemory(typeRegistry, fileData, state, systemSettings)) {
		return false;
	}

	state.path = std::string(path);
	return true;
}
