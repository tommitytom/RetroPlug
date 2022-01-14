#include "WaveformUtil.h"

#include <algorithm>

#include "InterpolationUtil.h"

using namespace rp;

void WaveformUtil::generate(const Float32Buffer& samples, Waveform& waveform, size_t targetSize) {
	auto& points = waveform.linePoints;
	points.clear();

	if (samples.size() < targetSize) {
		f32 xOffset = 0;
		f32 step = (f32)targetSize / (f32)samples.size();

		for (size_t i = 0; i < samples.size(); ++i) {
			points.push_back({ xOffset, samples[i] });
			xOffset += step;
		}
	} else {
		f32 chunkSize = (f32)((f32)samples.size() / (f32)targetSize);

		for (size_t i = 0; i < targetSize; ++i) {
			f32 min = 1.0f;
			f32 max = -1.0f;

			for (size_t j = 0; j < chunkSize; ++j) {
				f32 sample = samples[i * (size_t)chunkSize + j];
				sample = MathUtil::clamp(sample, -1.0f, 1.0f);
				min = std::min(sample, min);
				max = std::max(sample, max);
			}

			points.push_back({ (f32)i, max });
			points.push_back({ (f32)i, min });
		}
	}
}
