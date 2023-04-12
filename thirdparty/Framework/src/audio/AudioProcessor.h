#pragma once

#include "foundation/DataBuffer.h"
#include "foundation/Event.h"
#include "foundation/Types.h"
#include "audio/MidiMessage.h"

namespace fw {
	class AudioProcessor {
	private:
		EventNode _eventNode;
		f32 _sampleRate = 48000;

	public:
		AudioProcessor(): _eventNode("Audio") {}
		~AudioProcessor() = default;

		virtual void onRender(f32* output, const f32* input, uint32 frameCount) = 0;

		virtual void onTransportChange(bool playing) {}

		virtual void onMidi(const fw::MidiMessage& message) {}

		virtual void onSampleRateChange(f32 sampleRate) {}

		virtual void onSerialize(fw::Uint8Buffer& target) {}

		virtual void onDeserialize(const fw::Uint8Buffer& source) {}

		void setSampleRate(f32 sampleRate) { 
			_sampleRate = sampleRate;
			onSampleRateChange(sampleRate);
		}

		f32 getSampleRate() const {
			return _sampleRate;
		}

		EventNode& getEventNode() {
			return _eventNode;
		}

		const EventNode& getEventNode() const {
			return _eventNode;
		}
	};

	using AudioProcessorPtr = std::shared_ptr<AudioProcessor>;
}
