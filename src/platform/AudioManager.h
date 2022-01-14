#pragma once

#include <functional>

#include "platform/Types.h"

namespace rp {
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
	};

#else

	class AudioManager {
	private:
		struct State;

		State* _state = nullptr;
		AudioCallback _cb;

	public:
		AudioManager();
		~AudioManager();

		bool loadFile(std::string_view path, std::vector<f32>& target);

		bool start();

		void setCallback(AudioCallback&& cb) {
			_cb = std::move(cb);
		}

		void stop();

		uint32 getSampleRate();

		AudioCallback& getCallback() {
			return _cb;
		}
	};

#endif
}
