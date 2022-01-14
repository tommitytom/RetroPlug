#include "ProjectSerializer.h"

#include <sol/sol.hpp>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>

#include "util/SolUtil.h"
#include "util/fs.h"

using namespace rp;

const std::string_view PROJECT_VERSION = "1.0.0";
const std::string_view RP_VERSION = "0.4.0";

bool ProjectSerializer::serialize(std::string_view path, ProjectState& state, bool updatePath) {
	sol::state s;
	s.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);

	SolUtil::addIncludePath(s, "../../src/scripts");

	sol::table output = s.create_table_with(
		"path", path,
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

	for (const auto& [id, system] : state.systemSettings) {
		sol::table systemTable = s.create_table_with(
			"romPath", system.romPath,
			"sramPath", system.sramPath,
			"includeRom", system.includeRom,
			"input", s.create_table_with(
				"key", system.input.key,
				"pad", system.input.pad
			)
		);

		sol::table modelTable = systemTable.create_named("model");

		for (auto& [modelType, model] : system.models) {
			model->serialize(s, modelTable.create_named(model->getName()));
		}

		systemsTable.add(systemTable);
	}

	output["systems"] = systemsTable;

	std::string target;
	if (SolUtil::serializeTable(s, output, target)) {
		if (fsutil::writeTextFile(path, target)) {
			spdlog::info("Successfully wrote project file to {}", path);

			if (updatePath) {
				state.path = std::string(path);
			}
			
			return true;
		} else {
			spdlog::error("Failed to save project to file");
			return false;
		}
	} else {
		spdlog::error("Failed to serialize project: {}", target);
		return false;
	}
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

bool ProjectSerializer::deserialize(std::string_view path, ProjectState& state) {
	sol::state s;
	s.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);

	SolUtil::addIncludePath(s, "../../src/scripts");

	std::string fileData = fsutil::readTextFile(path);
	
	sol::table target;
	bool ok = SolUtil::deserializeTable(s, fileData, target);

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

	for (size_t i = 0; i < systemsTable.size(); ++i) {
		sol::table systemTable = systemsTable[i + 1];
		if (systemTable.valid() == false) {
			spdlog::error("Failed to load system");
			continue;
		}

		sol::table modelTable = systemTable["model"];
		std::string serializedState;
		if (!SolUtil::serializeTable(s, modelTable, serializedState)) {
			spdlog::error("Failed to reserialize state during project load");
			return false;
		}
		
		state.systemSettings[i] = SystemSettings {
			.romPath = systemTable["romPath"],
			.sramPath = systemTable["sramPath"],
			.includeRom = systemTable["includeRom"],
			.input = SystemSettings::InputSettings {
				.key = systemTable["input"]["key"],
				.pad = systemTable["input"]["pad"]
			},
			.serialized = std::move(serializedState)
		};
	}	

	return true;
}
