#include "ProjectSerializer.h"

#include <sol/sol.hpp>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "foundation/MetaUtil.h"

#include "core/LuaUtil.h"

using namespace rp;

const std::string_view PROJECT_VERSION = "1.0.0";
const std::string_view RP_VERSION = "0.4.0";

std::string ProjectSerializer::serialize(const ProjectState& state, const std::vector<SystemWrapperPtr>& systems) {
	sol::state s;
	rp::LuaUtil::prepareState(s);

	sol::table output = s.create_table_with(
		"path", state.path,
		"projectVersion", PROJECT_VERSION,
		"retroPlugVersion", RP_VERSION,
		"settings", s.create_table_with(
			"audioRouting", magic_enum::enum_name(state.settings.audioRouting),
			"midiRouting", magic_enum::enum_name(state.settings.midiRouting),
			"layout", magic_enum::enum_name(state.settings.layout),
			"saveType", magic_enum::enum_name(state.settings.saveType),
			"zoom", state.settings.zoom,
			"includeRom", state.settings.includeRom
		)
	);

	sol::table systemsTable = s.create_table();

	for (const SystemWrapperPtr& system : systems) {
		const SystemDesc& systemDesc = system->getDesc();

		sol::table descTable = s.create_table_with(
			"paths", s.create_table_with(
				"romPath", systemDesc.paths.romPath,
				"sramPath", systemDesc.paths.sramPath
			),
			"settings", s.create_table_with(
				"includeRom", systemDesc.settings.includeRom,
				"input", s.create_table_with(
					"key", systemDesc.settings.input.key,
					"pad", systemDesc.settings.input.pad
				),
				"model", s.create_table()
			)
		);

		sol::table modelTable = descTable["settings"]["model"];

		for (auto& [modelType, model] : system->getModels()) {
			std::string_view typeName = fw::MetaUtil::getTypeName(modelType);
			model->onSerialize(s, modelTable.create_named(typeName));
		}

		systemsTable.add(descTable);
	}

	output["systems"] = systemsTable;

	std::string target;
	if (fw::SolUtil::serializeTable(s, output, target)) {
		return target;
	} else {
		spdlog::error("Failed to serialize project: {}", target);
		return "{}";
	}
}

std::string ProjectSerializer::serializeModels(SystemWrapperPtr system) {
	sol::state s;
	rp::LuaUtil::prepareState(s);

	sol::table modelTable = s.create_table();
	for (auto& [modelType, model] : system->getModels()) {
		std::string_view typeName = fw::MetaUtil::getTypeName(modelType);
		model->onSerialize(s, modelTable.create_named(typeName));
	}

	std::string target;
	if (fw::SolUtil::serializeTable(s, modelTable, target)) {
		return target;
	}

	spdlog::error("Failed to serialize system models");
	return "";
}

bool ProjectSerializer::serialize(std::string_view path, ProjectState& state, const std::vector<SystemWrapperPtr>& systems, bool updatePath) {
	std::string output = serialize(state, systems);
	if (output.size()) {
		if (fw::FsUtil::writeTextFile(path, output)) {
			spdlog::info("Successfully wrote project file to {}", path);

			if (updatePath) {
				state.path = std::string(path);
			}

			return true;
		} else {
			spdlog::error("Failed to save project to file");
			return false;
		}

		return true;
	}

	return false;
}

template <typename T>
bool deserializeEnum(const sol::table& source, std::string_view name, T& target) {
	auto valueCast = magic_enum::enum_cast<T>(source[name].get<std::string>());

	if (valueCast.has_value()) {
		target = valueCast.value();
		return true;
	}

	spdlog::warn("Failed to deserialize enum value {}", name);

	return false;
}

bool ProjectSerializer::deserialize(std::string_view path, ProjectState& state, std::vector<SystemDesc>& systemSettings) {
	sol::state s;
	rp::LuaUtil::prepareState(s);

	std::string fileData = fw::FsUtil::readTextFile(path);
	
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

	state.path = path;

	deserializeEnum<AudioChannelRouting>(settings, "audioRouting", state.settings.audioRouting);
	deserializeEnum<MidiChannelRouting>(settings, "midiRouting", state.settings.midiRouting);
	deserializeEnum<SystemLayout>(settings, "layout", state.settings.layout);
	deserializeEnum<SaveStateType>(settings, "saveType", state.settings.saveType);

	state.settings.zoom = settings["zoom"].get<int>();
	state.settings.includeRom = settings["includeRom"].get<bool>();

	sol::table systemsTable = target["systems"];

	for (SystemId i = 0; i < (SystemId)systemsTable.size(); ++i) {
		sol::table systemTable = systemsTable[i + 1];
		if (systemTable.valid() == false) {
			spdlog::error("Failed to load system");
			continue;
		}

		sol::table settingsTable = systemTable["settings"];
		sol::table pathsTable = systemTable["paths"];

		sol::table modelTable = settingsTable["model"];
		std::string serializedState;
		if (!fw::SolUtil::serializeTable(s, modelTable, serializedState)) {
			spdlog::error("Failed to reserialize state during project load");
			return false;
		}

		systemSettings.push_back(SystemDesc{
			.paths = {
				.romPath = pathsTable["romPath"],
				.sramPath = pathsTable["sramPath"].get_or(std::string()),
			},
			.settings = {
				.includeRom = settingsTable["includeRom"],
				.input = SystemSettings::InputSettings {
					.key = settingsTable["input"]["key"],
					.pad = settingsTable["input"]["pad"]
				},
				.serialized = std::move(serializedState)
			}
		});
	}	

	return true;
}
