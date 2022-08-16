#pragma once

#include <spdlog/spdlog.h>

#include "audio/AudioProcessor.h"
#include "core/AudioState.h"
#include "core/System.h"
#include "core/SystemProcessor.h"
#include "core/UiState.h"
#include "util/CircularBuffer.h"

namespace rp {
	class AudioContext final : public AudioProcessor {
	private:
		const size_t MIN_LATENCY = 48 * 100;
		const size_t MAX_LATENCY = 48 * 200;

		CircularBuffer<f32> _buffer;
		bool _fillingBuffer = true;

		AudioState _state;

		IoMessageBus* _ioMessageBus;
		OrchestratorMessageBus* _orchestratorMessageBus;

		//size_t _currentBufferPos = 0;

	public:
		AudioContext(IoMessageBus* messageBus, OrchestratorMessageBus* orchestratorMessageBus);
		~AudioContext() {}

		void onRender(f32* output, const f32* input, uint32 frameCount) override;

		void setSampleRate(uint32 sampleRate);
		
		AudioState& getState() {
			return _state;
		}
	};
}
