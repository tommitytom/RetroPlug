#pragma once

#include "foundation/Types.h"

namespace fw {
	struct TimeInfo {
		f64 sampleRate = 44100.0;
		f64 tempo = 120.0;
		//f64 samplePos = -1.0;
		f64 ppqPos = -1.0;
		//f64 lastBar = -1.0;
		f64 cycleStart = -1.0;
		f64 cycleEnd = -1.0;

		int32 numerator = 4;
		int32 denominator = 4;
		uint32 frameCount = 0;

		bool transportIsRunning = false;
		bool transportLoopEnabled = false;
	};
}