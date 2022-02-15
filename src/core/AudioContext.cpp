#include "AudioContext.h"

#include "sameboy/SameBoyManager.h"
#include "core/AudioStreamSystem.h"

using namespace rp;

AudioContext::AudioContext(IoMessageBus* messageBus, OrchestratorMessageBus* orchestratorMessageBus) 
	: _buffer(MAX_LATENCY + 2), _ioMessageBus(messageBus), _orchestratorMessageBus(orchestratorMessageBus) 
{
	_buffer.clear();
	_state.processor.addManager<SameBoyManager>();
	_state.processor.addManager<SystemManager<AudioStreamSystem>>();
}

void AudioContext::process(f32* target, uint32 frameCount) {
	size_t sampleCount = (size_t)frameCount * 2;

	OrchestratorChange change;
	while (_orchestratorMessageBus->uiToAudio.try_dequeue(change)) {
		if (change.add) {
			_state.processor.addSystem(change.add);
		}

		if (change.remove != INVALID_SYSTEM_ID) {
			_state.processor.removeSystem(change.remove);
		}

		if (change.replace) {
			SystemPtr old = _state.processor.removeSystem(change.replace->getId());
			_state.processor.addSystem(change.replace);
			change.replace = old;

			_orchestratorMessageBus->audioToUi.enqueue(change);
		}

		if (change.swap) {
			SystemPtr old = _state.processor.removeSystem(change.swap->getId());
			_state.processor.addSystem(change.swap);
			change.swap = old;

			_orchestratorMessageBus->audioToUi.enqueue(change);
		}

		if (change.reset != INVALID_SYSTEM_ID) {
			SystemPtr system = _state.processor.findSystem(change.reset);
			if (system) {
				system->reset();
			}
		}
	}

	SystemIoPtr stream;
	while (_ioMessageBus->uiToAudio.try_dequeue(stream)) {
		SystemPtr system = _state.processor.findSystem(stream->systemId);
		if (system) {
			if (system->getStream()) {
				system->getStream()->merge(*stream);
				_ioMessageBus->dealloc(std::move(stream));
			} else {
				system->setStream(std::move(stream));
			}
		} else {
			_ioMessageBus->dealloc(std::move(stream));
		}
	}

	// Make sure systems have output buffers set
	for (SystemPtr& system : _state.processor.getSystems()) {
		SystemIoPtr& io = system->getStream();
		if (!io) {
			io = _ioMessageBus->alloc(system->getId());
		}

		if (io) {
			io->output.audio = std::make_shared<Float32Buffer>(sampleCount);
		}
	}
	
	Float32Buffer buffer(target, sampleCount);
	buffer.clear();

	_state.processor.process(frameCount);

	// Combine output of systems!

	for (SystemPtr& system : _state.processor.getSystems()) {
		SystemIoPtr& io = system->getStream();
		if (io && io->output.audio) {
			for (uint32 i = 0; i < sampleCount; ++i) {
				buffer[i] = buffer[i] + io->output.audio->get(i);
			}
		}
	}

	/*if (_buffers.size() > 4) {
	while (_buffers.size()) {
	_buffers.pop();
	}

	_currentBufferPos = 0;
	}

	size_t sampleCount = frameCount * 2;
	size_t targetPos = 0;

	Float32Buffer buffer(target, sampleCount);
	buffer.clear();

	while (targetPos < sampleCount && _buffers.size() > 0) {
	Float32BufferPtr b = _buffers.front();

	size_t bufferRemain = b->size() - _currentBufferPos;
	size_t targetRemain = sampleCount - targetPos;

	if (bufferRemain <= targetRemain) {
	buffer.slice(targetPos, bufferRemain).copyFrom(b->slice(_currentBufferPos, bufferRemain));
	targetPos += bufferRemain;
	_currentBufferPos = 0;

	_buffers.pop();
	} else {
	buffer.slice(targetPos, targetRemain).copyFrom(b->slice(_currentBufferPos, targetRemain));
	targetPos += targetRemain;
	_currentBufferPos += targetRemain;
	}
	}*/

	//_buffer.read(buffer, true);

	std::vector<SystemPtr>& systems = _state.processor.getSystems();
	for (SystemPtr& system : systems) {
		SystemIoPtr stream = std::move(system->getStream());

		if (stream) {
			// Clear inputs
			stream->input.reset();

			// Pass IO buffers to UI thread
			if (!_ioMessageBus->audioToUi.try_emplace(std::move(stream))) {
				spdlog::warn("Failed to pass IO buffer to UI thread");
			}
		}
	}
}

void AudioContext::setSampleRate(uint32 sampleRate) {
	std::vector<SystemPtr>& systems = _state.processor.getSystems();
	for (SystemPtr& system : systems) {
		system->setSampleRate(sampleRate);
	}
}
