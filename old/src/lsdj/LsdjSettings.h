#pragma once

#include <string>
#include <vector>

#include "foundation/Types.h"
#include "core/Forward.h"
#include "lsdj/Ram.h"

namespace rp {
	enum class LsdjSyncMode {
		Off,
		MidiSync,
		MidiSyncArduinoboy,
		MidiMap,
		Keyboard,
		KeyboardMidi,
		MidiPassthrough
	};

	struct SampleSettings {
		int32 dither = 0xFF;
		int32 volume = 0xFF;
		int32 gain = 0x1;
		int32 pitch = 0x7F;
		int32 filter = 0;
		int32 cutoff = 0x7F;
		int32 q = 0;
	};

	const SampleSettings EMPTY_SAMPLE_SETTINGS = SampleSettings{
		.dither = -1,
		.volume = -1,
		.gain = -1,
		.pitch = -1,
		.filter = -1,
		.cutoff = -1,
		.q = -1,
	};

	struct KitSample {
		std::string name;
		std::string path;
		SampleSettings settings = EMPTY_SAMPLE_SETTINGS;
	};

	struct KitState {
		std::string name;
		std::vector<KitSample> samples;
		SampleSettings settings;
	};

	using KitIndex = uint32;

	struct LsdjServiceSettings {
		std::unordered_map<KitIndex, KitState> kits;
		KitIndex kit = 0;

		lsdj::MemoryOffsets ramOffsets;
		bool romValid = false;
		bool offsetsValid = false;
	};

	struct ArduinoboyServiceSettings {
		LsdjSyncMode syncMode = LsdjSyncMode::Off;
		bool autoPlay = false;
		uint32 tempoDivisor = 1;
/*
		ArduinoboyServiceSettings& operator=(const ArduinoboyServiceSettings& other) {
			syncMode = other.syncMode;
			autoPlay = other.autoPlay;
			tempoDivisor = other.tempoDivisor;
			return *this;
		}

		ArduinoboyServiceSettings& operator=(ArduinoboyServiceSettings&& other) noexcept {
			syncMode = other.syncMode;
			autoPlay = other.autoPlay;
			tempoDivisor = other.tempoDivisor;
			other.syncMode = LsdjSyncMode::Off;
			other.autoPlay = false;
			other.tempoDivisor = 1;
			return *this;
		}
		*/
	};

	const SystemServiceType LSDJ_SERVICE_TYPE = 0x15D115D1;
	const SystemServiceType ARDUINOBOY_SERVICE_TYPE = 0x421D1B01;
}
