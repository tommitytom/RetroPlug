#pragma once

#include "Types.h"
#include "util/DataBuffer.h"
#include <string>
#include <vector>
#include "Constants.h"
#include "model/ButtonStream.h"

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

struct SystemDesc {
	SystemIndex idx = NO_ACTIVE_SYSTEM;
	SystemType emulatorType = SystemType::Unknown;
	SystemState state = SystemState::Uninitialized;
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

	GameboyButtonStream buttons;

	bool fastBoot = false;
};

using SystemDescPtr = std::shared_ptr<SystemDesc>;

struct Project {
	struct Settings {
		AudioChannelRouting audioRouting = AudioChannelRouting::StereoMixDown;
		MidiChannelRouting midiRouting = MidiChannelRouting::SendToAll;
		SystemLayout layout = SystemLayout::Auto;
		SaveStateType saveType = SaveStateType::Sram;
		int zoom = 2;
	} settings;

	std::string path;
	std::vector<SystemDescPtr> systems;
	SystemIndex selectedSystem = NO_ACTIVE_SYSTEM;
};
