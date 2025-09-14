#pragma once

#ifdef FW_PLATFORM_WEB
#include "AudioManager.h"
#include "AudioBuffer.h"

namespace fw::audio {
	class WebAudioManager final : public AudioManager {
	private:
		fw::StereoAudioBuffer _input;
		fw::StereoAudioBuffer _output;
		int _audioContextId;
		bool _running = false;
		f32 _sampleRate = 48000.0f;

	public:
		WebAudioManager(int audioContextId, f32 sampleRate);
		~WebAudioManager();

		bool loadFile(std::string_view path, std::vector<f32>& target) override;

		bool start(int32 idx) override;

		void stop() override;

		f32 getSampleRate() override;

		bool setAudioDevice(int32 idx) override;

		fw::StereoAudioBuffer& getInput() {
			return _input;
		}

		fw::StereoAudioBuffer& getOutput() {
			return _output;
		}
	};

	using WebAudioManagerPtr = std::shared_ptr<WebAudioManager>;
}
#endif
