#pragma once

#include <spdlog/spdlog.h>

#include "core/AudioState.h"
#include "core/UiState.h"
#include "core/Proxies.h"
#include "core/System.h"
#include "core/SystemProcessor.h"
#include "util/CircularBuffer.h"

namespace rp {
	class AudioContext {
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

		void process(f32* target, uint32 frameCount);

		void setSampleRate(uint32 sampleRate);
		
		AudioState& getState() {
			return _state;
		}
	};
}
