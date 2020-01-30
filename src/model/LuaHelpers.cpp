#include "LuaHelpers.h"

#include "platform/Logger.h"
#include "model/Project.h"
#include "config/config.h"

bool validateResult(const sol::protected_function_result& result, const std::string& prefix, const std::string& name) {
	if (!result.valid()) {
		sol::error err = result;
		std::string what = err.what();
		consoleLog(prefix);
		if (!name.empty()) {
			consoleLog(" " + name);
		}

		consoleLogLine(": " + what);
		return false;
	}

	return true;
}

void setupCommon(sol::state* state) {
	sol::state& s = *state;

	s.new_enum("EmulatorInstanceState",
		"Uninitialized", EmulatorInstanceState::Uninitialized,
		"Initialized", EmulatorInstanceState::Initialized,
		"RomMissing", EmulatorInstanceState::RomMissing,
		"Running", EmulatorInstanceState::Running
	);

	s.new_enum("EmulatorType",
		"Unknown", EmulatorType::Unknown,
		"Placeholder", EmulatorType::Placeholder,
		"SameBoy", EmulatorType::SameBoy
	);

	s.new_enum("AudioChannelRouting",
		"StereoMixDown", AudioChannelRouting::StereoMixDown,
		"TwoChannelsPerChannel", AudioChannelRouting::TwoChannelsPerChannel,
		"TwoChannelsPerInstance", AudioChannelRouting::TwoChannelsPerInstance
	);

	s.new_enum("MidiChannelRouting",
		"FourChannelsPerInstance", MidiChannelRouting::FourChannelsPerInstance,
		"OneChannelPerInstance", MidiChannelRouting::OneChannelPerInstance,
		"SendToAll", MidiChannelRouting::SendToAll
	);

	s.new_enum("InstanceLayout",
		"Auto", InstanceLayout::Auto,
		"Column", InstanceLayout::Column,
		"Grid", InstanceLayout::Grid,
		"Row", InstanceLayout::Row
	);

	s.new_enum("SaveStateType",
		"Sram", SaveStateType::Sram,
		"State", SaveStateType::State
	);

	s.new_enum("GameboyModel",
		"Auto", GameboyModel::Auto,
		"Agb", GameboyModel::Agb,
		"CgbC", GameboyModel::CgbC,
		"CgbE", GameboyModel::CgbE,
		"DmgB", GameboyModel::DmgB
	);

	s.new_usertype<SameBoySettings>("SameBoySettings",
		"model", &SameBoySettings::model,
		"gameLink", &SameBoySettings::gameLink
	);

	s.new_usertype<Project>("Project",
		"path", &Project::path,
		"instances", &Project::instances,
		"settings", &Project::settings
	);

	s.new_usertype<Project::Settings>("ProjectSettings",
		"audioRouting", &Project::Settings::audioRouting,
		"midiRouting", &Project::Settings::midiRouting,
		"layout", &Project::Settings::layout,
		"zoom", &Project::Settings::zoom,
		"saveType", &Project::Settings::saveType
	);

	s["_RETROPLUG_VERSION"].set(PLUG_VERSION_STR);
	s["_consolePrint"].set_function(consoleLog);
}
