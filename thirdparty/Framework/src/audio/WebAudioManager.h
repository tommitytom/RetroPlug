#pragma once

#ifdef RP_WEB
#include "AudioManager.h"

namespace fw::audio {
	class WebAudioManager final : public AudioManager {
	private:

	public:
		WebAudioManager();
		~WebAudioManager();

		bool loadFile(std::string_view path, std::vector<f32>& target) override;

		bool start() override;

		void stop() override;

		f32 getSampleRate() override;

		bool setAudioDevice(uint32 idx) override;

		void getDeviceNames(std::vector<std::string>& names) override;
	};
}
#endif
