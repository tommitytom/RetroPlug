#pragma once

#include <functional>
#include <cassert>

#include "foundation/Types.h"
#include "AudioProcessor.h"

namespace fw::audio {
	using AudioCallback = std::function<void(f32* output, const f32* input, uint32 frameCount)>;

	class AudioManager {
	protected:
		std::shared_ptr<AudioProcessor> _processor;

	public:
		AudioManager() {}
		~AudioManager() {}

		void setProcessor(std::shared_ptr<AudioProcessor> processor) {
			_processor = processor;
		}

		const std::shared_ptr<AudioProcessor>& getProcessor() const {
			return _processor;
		}

		void process(f32* output, const f32* input, uint32 frameCount) {
			_processor->onRender(output, input, frameCount);
		}

		virtual bool start() { return false; }

		virtual void stop() {}

		virtual void setSampleRate() {}

		virtual uint32 getSampleRate() { return 48000; }

		virtual bool setAudioDevice(uint32 idx) { return false; }

		virtual void getDeviceNames(std::vector<std::string>& names) {}

		virtual bool loadFile(std::string_view path, std::vector<f32>& target) { assert(false); return false; }
	};
}
