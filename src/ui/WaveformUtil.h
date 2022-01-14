#pragma once

#include "util/DataBuffer.h"
#include "util/MathUtil.h"

namespace rp {
	struct Waveform {
		std::vector<Point<f32>> linePoints;
	};
}

namespace rp::WaveformUtil {
	void generate(const Float32Buffer& samples, Waveform& target, size_t targetSize);
}
