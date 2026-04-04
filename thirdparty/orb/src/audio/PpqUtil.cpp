#include "PpqUtil.h"

#include <cmath>

namespace orb {
	void PpqUtil::eachTick(const orb::TimeInfo& time, uint32 resolution, std::function<void(uint32 ppq, uint32 offset)>&& func) {
		const f64 samplesPerMs = time.sampleRate / 1000.0;
		const f64 beatLenMs = (60000.0 / time.tempo);
		const f64 beatLenSamples = beatLenMs * samplesPerMs;
		const f64 beatLenSamples24 = beatLenSamples / resolution;

		const f64 ppq24 = time.ppqPos * resolution;
		const f64 framePpqLen = (time.frameCount / beatLenSamples) * resolution;
		const f64 framePpqEnd = ppq24 + framePpqLen;

		f64 lastPpq24 = ppq24;
		f64 nextPpq24 = std::ceil(ppq24);
		f64 offset = 0;

		// TODO: Worth putting a check here to avoid potential infinite while loop?

		while (nextPpq24 < framePpqEnd) {
			f64 amount = nextPpq24 - lastPpq24;
			offset += beatLenSamples24 * amount;

			if (offset >= time.frameCount) {
				//consoleLogLine(("Overshot: " + std::to_string(offset - sampleCount)));
				offset = time.frameCount - 1;
			}

			if (offset < 0.0) {
				offset = 0.0;
			}

			func(static_cast<uint32>(nextPpq24), static_cast<uint32>(offset));

			lastPpq24 = nextPpq24;
			nextPpq24 += 1.0;
		}
	}
}