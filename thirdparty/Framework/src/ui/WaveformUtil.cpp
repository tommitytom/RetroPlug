#include "WaveformUtil.h"

#include <algorithm>

#include "foundation/InterpolationUtil.h"

using namespace fw;

void WaveformUtil::generate(const Float32Buffer& samples, Waveform& waveform, size_t targetSize, size_t channel, size_t channelCount) {
	assert((samples.size() % channelCount) == 0);

	const size_t frameCount = samples.size() / channelCount;

	if (frameCount < 2 || targetSize < 2) {
		return;
	}

	auto& points = waveform.linePoints;
	points.clear();

	if (frameCount < targetSize) {
		const f32 step = (f32)targetSize / (f32)(frameCount - 1);
		f32 xOffset = 0;

		for (size_t i = 0; i < samples.size(); i += channelCount) {
			if (i > channelCount) {
				points.push_back(points.back());
			}

			points.push_back({ xOffset, samples[i + channel] });
			xOffset += step;
		}
	} else {
		const f32 fullChunkSize = (f32)frameCount / (f32)targetSize;
		const size_t chunkSize = (size_t)fullChunkSize;
		const f32 chunkRemain = fmod(fullChunkSize, 1);

		f32 overflow = 0.0f;
		size_t nextChunkSize = chunkSize;
		size_t chunkPos = 0;

		f32 lastMin = 0.0f;
		f32 lastMax = 0.0f;

		for (size_t i = 0; i < targetSize; ++i) {
			const size_t chunkEnd = chunkPos + nextChunkSize;

			f32 min = 1.0f;
			f32 max = -1.0f;

			for (chunkPos; chunkPos < chunkEnd; chunkPos++) {
				f32 sample = samples[chunkPos * channelCount + channel];
				sample = MathUtil::clamp(sample, -1.0f, 1.0f);
				min = std::min(sample, min);
				max = std::max(sample, max);
			}

			if (i > 0) {
				if (min > lastMax) {
					points.push_back({ (f32)i - 1, lastMax });
					points.push_back({ (f32)i, min });
				}

				if (max < lastMin) {
					points.push_back({ (f32)i - 1, lastMin });
					points.push_back({ (f32)i, max });
				}
			}

			lastMin = min;
			lastMax = max;

			points.push_back({ (f32)i, max });
			points.push_back({ (f32)i, min });

			overflow += chunkRemain;

			if (overflow >= 1.0f) {
				assert(overflow < 2.0f);
				overflow -= 1.0f;
				nextChunkSize = chunkSize + 1;
			} else {
				nextChunkSize = chunkSize;
			}
		}
	}
}
