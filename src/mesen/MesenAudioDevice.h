#pragma once

#include "Core/Shared/Interfaces/IAudioDevice.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace rp {
	// IAudioDevice implementation that captures rendered samples into an
	// in-process ring buffer instead of sending them to a hardware device.
	class MesenAudioDevice final : public IAudioDevice {
	public:
		// PlayBuffer is called by SoundMixer after resampling.
		// samples: interleaved stereo int16, count = number of stereo frames.
		void PlayBuffer(int16_t* samples, uint32_t count, uint32_t sampleRate, bool isStereo) override {
			_counter += count;
			/*std::lock_guard<std::mutex> lock(_mutex);
			size_t base = _buffer.size();
			_buffer.resize(base + count * 2);
			memcpy(_buffer.data() + base, samples, count * 2 * sizeof(int16_t));*/
		}

		void Stop() override {}
		void Pause() override {}
		void ProcessEndOfFrame() override {}
		string GetAvailableDevices() override { return {}; }
		void SetAudioDevice(string) override {}
		AudioStatistics GetStatistics() override { return {}; }

		// Returns how many stereo frames are currently buffered.
		size_t availableFrames() const {
			//std::lock_guard<std::mutex> lock(_mutex);
			//return _buffer.size() / 2;
			return _counter;
		}

		// Drain up to `frameCount` stereo frames into `dest` as normalised float32.
		// Returns the number of frames actually written.
		uint32_t drain(float* dest, uint32_t frameCount) {
			/*std::lock_guard<std::mutex> lock(_mutex);
			uint32_t have = (uint32_t)(_buffer.size() / 2);
			uint32_t take = std::min(have, frameCount);
			constexpr float kScale = 1.0f / 32768.0f;
			for (uint32_t i = 0; i < take * 2; ++i) {
				dest[i] = _buffer[i] * kScale;
			}
			_buffer.erase(_buffer.begin(), _buffer.begin() + take * 2);
			return take;*/

			size_t take = std::min(_counter, static_cast<size_t>(frameCount));
			_counter -= take;
			//memset(dest, 0, frameCount * sizeof(float));
			return frameCount;
		}

	private:
		size_t _counter = 0;
		mutable std::mutex		_mutex;
		std::vector<int16_t>	_buffer;
	};
}
