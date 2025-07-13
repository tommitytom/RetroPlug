#pragma once

#include "AudioManager.h"

namespace fw::audio {
	class MiniAudioManager final : public AudioManager {
	private:
		struct State;
		State* _state = nullptr;

		bool _active = false;
		int32 _outputIdx = -1;

	public:
		MiniAudioManager();
		~MiniAudioManager();

		bool loadFile(std::string_view path, std::vector<f32>& target) override;

		bool start(int32 idx) override;

		void stop() override;

		f32 getSampleRate() override;

		bool setAudioDevice(int32 idx) override;

		std::string getActiveOutputName() override;

		void getDeviceNames(std::vector<std::string>& inputs, std::vector<std::string>& outputs) override;
	};
}
