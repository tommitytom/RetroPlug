#pragma once

#include <string>
#include <map>
#include "platform/Types.h"
#include "SystemSettings.h"

namespace rp {
	/*enum class SystemType {
		Unknown,
		Placeholder,
		SameBoy
	};*/

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
		} settings;

		std::map<SystemId, SystemSettings> systemSettings;

		std::string path;
	};
}
