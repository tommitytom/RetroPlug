#pragma once

#include "Types.h"
#include "util/DataBuffer.h"
#include <string>
#include <vector>

struct EmulatorInstanceDesc {
	InstanceIndex idx = NO_ACTIVE_INSTANCE;
	EmulatorType emulatorType = EmulatorType::Unknown;
	EmulatorInstanceState state = EmulatorInstanceState::Uninitialized;
	std::string romName;
	std::string romPath;
	std::string savPath;

	DataBufferPtr sourceRomData;
	DataBufferPtr patchedRomData;

	DataBufferPtr sourceStateData;

	DataBufferPtr sourceSavData;
	DataBufferPtr patchedSavData;

	bool fastBoot = false;
};

struct Project {
	struct Settings {
		AudioChannelRouting audioRouting = AudioChannelRouting::StereoMixDown;
		MidiChannelRouting midiRouting = MidiChannelRouting::SendToAll;
		InstanceLayout layout = InstanceLayout::Auto;
		SaveStateType saveType = SaveStateType::Sram;
		int zoom = 2;
	} settings;

	std::string path;
	std::vector<EmulatorInstanceDesc> instances;
};
