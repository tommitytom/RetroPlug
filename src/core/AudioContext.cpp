#include "AudioContext.h"

#include "sameboy/SameBoyManager.h"
#include "core/AudioStreamSystem.h"

#include "core/PassthroughMidiProcessor.h"

using namespace rp;

AudioContext::AudioContext(IoMessageBus* messageBus, OrchestratorMessageBus* orchestratorMessageBus) 
	: _buffer(MAX_LATENCY + 2), _ioMessageBus(messageBus), _orchestratorMessageBus(orchestratorMessageBus) 
{
	_buffer.clear();
	_state.processor.addManager<SameBoyManager>();
	_state.processor.addManager<SystemManager<AudioStreamSystem>>();
}

MidiProcessorPtr getMidiProcessor(std::string_view romName) {
	if (romName == "MGB") {
		return std::make_shared<PassthroughMidiProcessor>();
	}

	return nullptr;
}

void AudioContext::processMessageBus() {
	OrchestratorChange change;
	while (_orchestratorMessageBus->uiToAudio.try_dequeue(change)) {
		if (change.add) {
			_midiProcessors.push_back(getMidiProcessor(change.add->getRomName()));
			_state.processor.addSystem(change.add);
		}

		if (change.remove != INVALID_SYSTEM_ID) {
			for (size_t i = 0; i < _state.processor.getSystems().size(); ++i) {
				SystemPtr system = _state.processor.getSystems()[i];
				if (system->getId() == change.remove) {
					_midiProcessors.erase(_midiProcessors.begin() + i);
					break;
				}
			}

			_state.processor.removeSystem(change.remove);
		}

		if (change.replace) {
			for (size_t i = 0; i < _state.processor.getSystems().size(); ++i) {
				SystemPtr system = _state.processor.getSystems()[i];
				if (system->getId() == change.replace->getId()) {
					_midiProcessors[i] = getMidiProcessor(change.add->getRomName());
					break;
				}
			}

			SystemPtr old = _state.processor.removeSystem(change.replace->getId());
			_state.processor.addSystem(change.replace);
			change.replace = old;

			_orchestratorMessageBus->audioToUi.enqueue(change);
		}

		if (change.swap) {
			for (size_t i = 0; i < _state.processor.getSystems().size(); ++i) {
				SystemPtr system = _state.processor.getSystems()[i];
				if (system->getId() == change.replace->getId()) {
					_midiProcessors[i] = getMidiProcessor(change.add->getRomName());
					break;
				}
			}

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

		if (change.gameLink != INVALID_SYSTEM_ID) {
			SystemPtr system = _state.processor.findSystem(change.gameLink);
			if (system) {
				if (!system->getGameLink()) {
					for (SystemPtr other : _state.processor.getSystems()) {
						if (other->getGameLink()) {
							other->addLinkTarget(system.get());
							system->addLinkTarget(other.get());
						}
					}

					system->setGameLink(true);
				} else {
					for (SystemPtr other : _state.processor.getSystems()) {
						if (other->getGameLink()) {
							other->removeLinkTarget(system.get());
						}
					}

					system->setGameLink(false);
				}
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
}

void AudioContext::onRender(f32* output, const f32* input, uint32 frameCount) {
	processMessageBus();

	size_t sampleCount = (size_t)frameCount * 2;

	// Make sure systems have output buffers set
	for (SystemPtr& system : _state.processor.getSystems()) {
		SystemIoPtr& io = system->getStream();
		if (!io) {
			io = _ioMessageBus->alloc(system->getId());
		}

		if (io) {
			io->output.audio = std::make_shared<fw::Float32Buffer>(sampleCount);
		}
	}

	fw::Float32Buffer buffer(output, sampleCount);
	buffer.clear();

	_state.processor.process(frameCount);

	// Combine output of systems!

	for (SystemPtr& system : _state.processor.getSystems()) {
		SystemIoPtr& io = system->getStream();
		if (io) {
			if (io->output.audio) {
				for (uint32 i = 0; i < sampleCount; ++i) {
					buffer[i] = buffer[i] + io->output.audio->get(i);
				}
			}

			if (io->output.serial.size()) {
				// TODO: Send midi data out
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

void AudioContext::onMidi(const fw::MidiMessage& message) {
	processMessageBus();

	const std::vector<SystemPtr>& systems = _state.processor.getSystems();
	uint32 channel = message.getChannel();

	/*for (SystemPtr& system : _state.processor.getSystems()) {
		SystemIoPtr& io = system->getStream();
		if (!io) {
			io = _ioMessageBus->alloc(system->getId());
		}
	}*/

	switch (_midiRouting) {
		case MidiChannelRouting::SendToAll: {
			for (size_t i = 0; i < systems.size(); i++) {
				MidiProcessorPtr processor = _midiProcessors[i];
				SystemIo& systemIo = *systems[i]->getStream();

				if (processor) {
					processor->onMidi(systemIo, message);
				}
			}

			break;
		}
		case MidiChannelRouting::OneChannelPerInstance: {
			if (channel < systems.size()) {
				MidiProcessorPtr processor = _midiProcessors[channel];
				SystemIo& systemIo = *systems[channel]->getStream();

				if (processor) {
					fw::MidiMessage msg = message;
					msg.setChannel(0);
					processor->onMidi(systemIo, msg);
				}
			}

			break;
		}
		case MidiChannelRouting::FourChannelsPerInstance: {
			if (channel < systems.size() * 4) {
				uint32 ch = channel % 4;
				MidiProcessorPtr processor = _midiProcessors[ch];
				SystemIo& systemIo = *systems[ch]->getStream();

				if (processor) {
					fw::MidiMessage msg = message;
					msg.setChannel(ch);
					processor->onMidi(systemIo, msg);
				}
			}

			break;
		}
	}
}

void AudioContext::setSampleRate(uint32 sampleRate) {
	std::vector<SystemPtr>& systems = _state.processor.getSystems();
	for (SystemPtr& system : systems) {
		system->setSampleRate(sampleRate);
	}
}
