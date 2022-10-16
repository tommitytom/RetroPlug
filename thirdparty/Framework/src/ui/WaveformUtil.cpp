#include "WaveformUtil.h"

#include <algorithm>

#include "foundation/InterpolationUtil.h"

using namespace fw;

void WaveformUtil::generate(const Float32Buffer& samples, Waveform& waveform, size_t targetSize, size_t channel, size_t channelCount) {
	assert((samples.size() % channelCount) == 0);

	size_t frameCount = samples.size() / channelCount;

	if (frameCount < 2 || targetSize < 2) {
		return;
	}

	auto& points = waveform.linePoints;
	points.clear();

	if (frameCount < targetSize) {
		f32 xOffset = 0;
		f32 step = (f32)targetSize / (f32)(frameCount - 1);

		for (size_t i = 0; i < samples.size(); i += channelCount) {
			if (i > channelCount) {
				points.push_back(points.back());
			}

			points.push_back({ xOffset, samples[i + channel] });
			xOffset += step;
		}
	} else {
		f32 chunkSize = (f32)frameCount / (f32)targetSize;

		for (size_t i = 0; i < targetSize; ++i) {
			f32 min = 1.0f;
			f32 max = -1.0f;

			for (size_t j = 0; j < chunkSize; ++j) {
				f32 sample = samples[(i * (size_t)chunkSize + j) * channelCount + channel];
				sample = MathUtil::clamp(sample, -1.0f, 1.0f);
				min = std::min(sample, min);
				max = std::max(sample, max);
			}

			points.push_back({ (f32)i, max });
			points.push_back({ (f32)i, min });
		}
	}
}
