#pragma once

#ifdef FW_PLATFORM_WEB
#include "AudioManager.h"
#include "AudioBuffer.h"

namespace fw::audio {
	class WebAudioManager final : public AudioManager {
	private:
		fw::StereoAudioBuffer _input;
		fw::StereoAudioBuffer _output;

	public:
		WebAudioManager();
		~WebAudioManager();

		bool loadFile(std::string_view path, std::vector<f32>& target) override;

		bool start() override;

		void stop() override;

		f32 getSampleRate() override;

		bool setAudioDevice(uint32 idx) override;

		void getDeviceNames(std::vector<std::string>& names) override;

		fw::StereoAudioBuffer& getInput() {
			return _input;
		}

		fw::StereoAudioBuffer& getOutput() {
			return _output;
		}
	};
}
#endif
