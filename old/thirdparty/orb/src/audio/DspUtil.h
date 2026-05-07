#pragma once
/*
namespace orb::DspUtil {
	// Apply gain to range of samples
	template <typename SampleType>
	void applyGainRamp(size_t channel, size_t startSample, size_t _sampleCountToRamp,
					SampleType startGain, SampleType endGain) {
		if (channel < _channelCount && startSample < _sampleCount) {
			size_t samplesToProcess = std::min(_sampleCountToRamp, _sampleCount - startSample);
			auto* data = &channels[channel][startSample];

			if (samplesToProcess > 1) {
				SampleType gainIncrement = (endGain - startGain) / SampleType(samplesToProcess - 1);
				for (size_t i = 0; i < samplesToProcess; ++i) {
					data[i] *= startGain + SampleType(i) * gainIncrement;
				}
			}
		}
	}

	// Find peak value in channel
	template <typename SampleType>
	SampleType findPeak(size_t channel) const {
		if (channel >= _channelCount) return SampleType(0);

		auto* data = channels[channel].get();
		SampleType peak = SampleType(0);

		for (size_t i = 0; i < _sampleCount; ++i) {
			peak = std::max(peak, std::abs(data[i]));
		}
		return peak;
	}

	// Find RMS value in channel
	template <typename SampleType>
	SampleType findRMS(size_t channel) const {
		if (channel >= _channelCount) return SampleType(0);

		auto* data = channels[channel].get();
		SampleType sum = SampleType(0);

		for (size_t i = 0; i < _sampleCount; ++i) {
			sum += data[i] * data[i];
		}

		return std::sqrt(sum / SampleType(_sampleCount));
	}

	// Reverse channel
	void reverse(size_t channel) {
		if (channel < _channelCount && _sampleCount > 0) {
			std::reverse(channels[channel].get(), channels[channel].get() + _sampleCount);
		}
	}

	// Convert from interleaved format
	void fromInterleaved(const SampleType* interleavedData, size_t channels, size_t samples) {
		resize(channels, samples);

		for (size_t ch = 0; ch < _channelCount; ++ch) {
			auto* dest = channels[ch].get();
			for (size_t s = 0; s < _sampleCount; ++s) {
				dest[s] = interleavedData[s * _channelCount + ch];
			}
		}
	}

	// Convert to interleaved format
	void toInterleaved(SampleType* interleavedData) const {
		for (size_t s = 0; s < _sampleCount; ++s) {
			for (size_t ch = 0; ch < _channelCount; ++ch) {
				interleavedData[s * _channelCount + ch] = channels[ch][s];
			}
		}
	}
}
*/