#pragma once

#include "util/DataBuffer.h"
#include "util/MathUtil.h"

namespace rp {
	struct Waveform {
		std::vector<PointT<f32>> linePoints;
	};
}

namespace rp::WaveformUtil {
	void generate(const Float32Buffer& samples, Waveform& target, size_t targetSize);
}
