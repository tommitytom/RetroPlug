#pragma once

const int MAX_INSTANCES = 4;

enum class EmulatorType {
	Unknown,
	Placeholder,
	SameBoy
};

enum class InstanceLayout {
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

enum class EmulatorInstanceState {
	Uninitialized,
	Initialized,
	RomMissing,
	Running
};

enum class SaveStateType {
	Sram,
	State
};

enum class DialogType {
	None,
	Save,
	Load
};
