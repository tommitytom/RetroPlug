#pragma once

#include <vector>

#include <spdlog/spdlog.h>

#include "foundation/CircularBuffer.h"

#include "audio/AudioProcessor.h"
#include "audio/MidiMessage.h"

#include "core/AudioState.h"
#include "core/MidiProcessor.h"
#include "core/System.h"
#include "core/SystemProcessor.h"
#include "core/UiState.h"

namespace rp {
	class AudioContext final : public fw::AudioProcessor {
	private:
		const size_t MIN_LATENCY = 48 * 100;
		const size_t MAX_LATENCY = 48 * 200;

		fw::CircularBuffer<f32> _buffer;
		bool _fillingBuffer = true;

		AudioState _state;

		IoMessageBus* _ioMessageBus;
		OrchestratorMessageBus* _orchestratorMessageBus;

		MidiChannelRouting _midiRouting = MidiChannelRouting::SendToAll;
		std::vector<MidiProcessorPtr> _midiProcessors;

		//size_t _currentBufferPos = 0;

	public:
		AudioContext(IoMessageBus* messageBus, OrchestratorMessageBus* orchestratorMessageBus);
		~AudioContext() {}

		void onRender(f32* output, const f32* input, uint32 frameCount) override;

		void onMidi(const fw::MidiMessage& message) override;

		void setSampleRate(uint32 sampleRate);
		
		AudioState& getState() {
			return _state;
		}

	private:
		void processMessageBus();
	};
}
