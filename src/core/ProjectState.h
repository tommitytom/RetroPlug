#pragma once

#include <map>
#include <string>
#include <refl.hpp>
#include "foundation/Types.h"
#include "SystemSettings.h"

namespace rp {
	enum class SystemLayout {
		Auto,
		Row,
		Column,
		Grid
	};

	enum class AudioChannelRouting {
		StereoMixDown,
		TwoChannelsPerInstance,
		TwoChannelsPerChannel
	};

	enum class MidiChannelRouting {
		SendToAll,
		FourChannelsPerInstance,
		OneChannelPerInstance
	};

	enum class SystemState {
		Uninitialized,
		Initialized,
		RomMissing,
		Running,
		VideoFeedLost
	};

	enum class SaveStateType {
		None,
		Sram,
		State
	};

	struct ProjectState {
		struct Settings {
			AudioChannelRouting audioRouting = AudioChannelRouting::StereoMixDown;
			MidiChannelRouting midiRouting = MidiChannelRouting::SendToAll;
			SystemLayout layout = SystemLayout::Auto;
			SaveStateType saveType = SaveStateType::Sram;
			int zoom = 2;
			bool includeRom = true;
			bool autoSave = true;
		} settings;

		std::string path;
	};

	struct GlobalConfig {
		ProjectState::Settings projectSettings;
		SystemSettings systemSettings;
	};

	/*REFL_TYPE(ProjectState, debug(debug_point), bases<>)
		REFL_FIELD(path, serializable()) // here we use serializable only as a maker
	REFL_END*/
}

struct serializable : refl::attr::usage::field, refl::attr::usage::function {};

REFL_AUTO(
	type(rp::ProjectState::Settings),
	field(audioRouting, serializable()),
	field(midiRouting, serializable()),
	field(layout, serializable()),
	field(saveType, serializable()),
	field(zoom, serializable()),
	field(includeRom, serializable()),
	field(autoSave, serializable())
)

REFL_AUTO(
	type(rp::ProjectState),
	field(settings, serializable()),
	field(path, serializable())
)
