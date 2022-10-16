#pragma once

#include "foundation/DataBuffer.h"
#include "foundation/MathUtil.h"
#include "audio/AudioBuffer.h"

namespace fw {
	struct Waveform {
		std::vector<PointF> linePoints;

		void clear() {
			linePoints.clear();
		}
	};

	struct MultiChannelWaveform {
		std::vector<Waveform> channels;

		void clear() {
			for (Waveform& waveform : channels) {
				waveform.clear();
			}
		}
	};
}

namespace fw::WaveformUtil {
	void generate(const Float32Buffer& samples, Waveform& target, size_t targetSize, size_t channel, size_t channelCount);
	
	template <const uint32 ChannelCount>
	MultiChannelWaveform generate(const InterleavedAudioBuffer<ChannelCount>& buffer, uint32 targetSize) {
		MultiChannelWaveform ret;

		for (uint32 i = 0; i < ChannelCount; ++i) {
			Waveform channel;
			generate(buffer.getSampleBuffer().slice((size_t)i), channel, targetSize, (size_t)ChannelCount);

			ret.channels.push_back(std::move(channel));
		}

		return ret;
	}
}
