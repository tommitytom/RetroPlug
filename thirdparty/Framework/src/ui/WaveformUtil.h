#pragma once

#include "foundation/DataBuffer.h"
#include "foundation/MathUtil.h"

namespace fw {
	struct Waveform {
		std::vector<PointF> linePoints;
	};
}

namespace fw::WaveformUtil {
	void generate(const Float32Buffer& samples, Waveform& target, size_t targetSize);
}
