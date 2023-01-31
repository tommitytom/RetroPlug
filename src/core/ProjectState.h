#pragma once

#include <map>
#include <string>
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
}
