#pragma once

#include <string>
#include <vector>

#include "foundation/Types.h"
#include "core/Forward.h"

namespace rp {
	enum class LsdjSyncMode {
		Off,
		MidiSync,
		MidiSyncArduinoboy,
		MidiMap
	};

	struct TimeInfo {
		f64 sampleRate = 44100.0;
		f64 tempo = 120.0;
		f64 ppqPos = 0;
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

	using KitIndex = size_t;

	struct LsdjServiceSettings {
		std::unordered_map<KitIndex, KitState> kits;
		KitIndex kit = 0;
	};

	struct ArduinoboyServiceSettings {
		LsdjSyncMode syncMode = LsdjSyncMode::Off;
		uint32 tempoDivisor = 1;
	};

	const SystemServiceType LSDJ_SERVICE_TYPE = 0x15D115D1;
	const SystemServiceType ARDUINOBOY_SERVICE_TYPE = 0x421D1B01;
}
