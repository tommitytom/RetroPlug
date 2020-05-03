#pragma once

#include "Types.h"
#include "util/DataBuffer.h"
#include <string>
#include <vector>
#include "Constants.h"

enum class GameboyModel {
	Auto,
	DmgB,
	//SgbNtsc,
	//SgbPal,
	//Sgb2,
	CgbC,
	CgbE,
	Agb
};

struct SameBoySettings {
	GameboyModel model = GameboyModel::Auto;
	bool gameLink = false;
};

struct EmulatorInstanceDesc {
	InstanceIndex idx = NO_ACTIVE_INSTANCE;
	EmulatorType emulatorType = EmulatorType::Unknown;
	EmulatorInstanceState state = EmulatorInstanceState::Uninitialized;
	std::string romName;
	std::string romPath;
	std::string savPath;

	SameBoySettings sameBoySettings;

	DataBufferPtr sourceRomData;
	DataBufferPtr patchedRomData;

	DataBufferPtr sourceStateData;

	DataBufferPtr sourceSavData;
	DataBufferPtr patchedSavData;

	std::string audioComponentState;
	std::string uiComponentState;

	bool fastBoot = false;
};

using EmulatorInstanceDescPtr = std::shared_ptr<EmulatorInstanceDesc>;

struct Project {
	struct Settings {
		AudioChannelRouting audioRouting = AudioChannelRouting::StereoMixDown;
		MidiChannelRouting midiRouting = MidiChannelRouting::SendToAll;
		InstanceLayout layout = InstanceLayout::Auto;
		SaveStateType saveType = SaveStateType::Sram;
		int zoom = 2;
	} settings;

	std::string path;
	std::vector<EmulatorInstanceDescPtr> instances;
};
