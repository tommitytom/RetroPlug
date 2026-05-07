#pragma once

const int MAX_SYSTEMS = 4;

enum class SystemType {
	Unknown,
	Placeholder,
	SameBoy
};

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

enum class DialogType {
	None,
	Load,
	Save,
	Directory
};
