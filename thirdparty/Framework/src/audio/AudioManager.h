#pragma once

#include <functional>

#include "foundation/Types.h"
#include "AudioProcessor.h"

namespace fw {
	using AudioCallback = std::function<void(f32* output, const f32* input, uint32 frameCount)>;

	struct AudioFile {

	};

#ifdef RP_WEB

	class AudioManager {
	public:
		bool start() { return true; }

		void setCallback(AudioCallback&& cb) {}

		void stop() {}

		uint32 getSampleRate() { return 48000; }

		void getDeviceNames(std::vector<std::string>& names) {}

		bool setAudioDevice(uint32 idx) { return true; }
	};

#else

	class AudioManager {
	private:
		struct State;

		State* _state = nullptr;
		std::shared_ptr<AudioProcessor> _processor;

	public:
		AudioManager();
		~AudioManager();

		void setProcessor(std::shared_ptr<AudioProcessor> processor) {
			_processor = processor;
		}

		const std::shared_ptr<AudioProcessor>& getProcessor() const {
			return _processor;
		}

		bool loadFile(std::string_view path, std::vector<f32>& target);

		bool start();

		void stop();

		uint32 getSampleRate();

		bool setAudioDevice(uint32 idx);

		void getDeviceNames(std::vector<std::string>& names);
	};

#endif
}
