#pragma once

#include <atomic>
#include <string>

enum class LsdjSyncModes {
	Off,
	Slave,
	Master,
	Midi,
	Nanoloop,
	Keyboard,
	AnalogIn,
	AnalogOut,
	MidiMap,
	MidiOut,
	SlaveArduinoboy
};

class Lsdj {
public:
	std::atomic<LsdjSyncModes> syncMode = LsdjSyncModes::Off;
	bool found = true;
	std::string version;
	bool arduinoboyPlaying = false;
	int tempoDivisor = 1;
};
