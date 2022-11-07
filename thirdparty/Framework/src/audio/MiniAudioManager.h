#pragma once

#include "AudioManager.h"

namespace fw::audio {
	class MiniAudioManager final : public AudioManager {
	private:
		struct State;
		State* _state = nullptr;

	public:
		MiniAudioManager();
		~MiniAudioManager();

		bool loadFile(std::string_view path, std::vector<f32>& target) override;

		bool start() override;

		void stop() override;

		uint32 getSampleRate() override;

		bool setAudioDevice(uint32 idx) override;

		void getDeviceNames(std::vector<std::string>& names) override;
	};
}
