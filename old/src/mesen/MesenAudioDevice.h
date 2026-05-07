#pragma once

#include "Core/Shared/Interfaces/IAudioDevice.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace rp {
	// IAudioDevice implementation that captures rendered samples into an
	// in-process ring buffer instead of sending them to a hardware device.
	class MesenAudioDevice final : public IAudioDevice {
	private:
		std::vector<int16_t> _buffer;

	public:
		// PlayBuffer is called by SoundMixer after resampling.
		// samples: interleaved stereo int16, count = number of stereo frames.
		void PlayBuffer(int16_t* samples, uint32_t count, uint32_t sampleRate, bool isStereo) override {
			size_t base = _buffer.size();
			_buffer.resize(base + count * 2);
			memcpy(_buffer.data() + base, samples, count * 2 * sizeof(int16_t));
		}

		void Stop() override {}
		void Pause() override {}
		void ProcessEndOfFrame() override {}
		string GetAvailableDevices() override { return {}; }
		void SetAudioDevice(string) override {}
		AudioStatistics GetStatistics() override { return {}; }

		// Returns how many stereo frames are currently buffered.
		size_t availableFrames() const {
			return _buffer.size() / 2;
		}

		// Drain up to `frameCount` stereo frames into `dest` as normalised float32.
		// Returns the number of frames actually written.
		uint32_t drain(float* dest, uint32_t frameCount) {
			uint32_t have = (uint32_t)(_buffer.size() / 2);
			uint32_t take = std::min(have, frameCount);
			constexpr float kScale = 1.0f / 32768.0f;
			for (uint32_t i = 0; i < take * 2; ++i) {
				dest[i] = _buffer[i] * kScale;
			}
			_buffer.erase(_buffer.begin(), _buffer.begin() + take * 2);
			return take;
		}
	};
}
