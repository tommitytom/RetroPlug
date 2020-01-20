#pragma once

#include "plugs/SameBoyPlug.h"
#include "controller/messaging.h"
#include "Constants.h"
#include "Types.h"
#include "micromsg/allocator/allocator.h"

struct AudioSettings {
	size_t channelCount;
	size_t frameCount;
	double sampleRate;
};

class ProcessingContext {
private:
	std::vector<SameBoyPlugPtr> _instances;
	Node* _node;

	Project::Settings _settings;
	AudioSettings _audioSettings;

	AudioBuffer _audioBuffers[MAX_INSTANCES];

	micromsg::Allocator* _alloc;

public:
	ProcessingContext() {
		_instances.reserve(MAX_INSTANCES);
		for (size_t i = 0; i < MAX_INSTANCES; ++i) {
			_instances.push_back(nullptr);
		}
	}
	~ProcessingContext() {}

	void setAudioSettings(const AudioSettings& settings) {
		_audioSettings = settings;
	}

	void setNode(Node* node) {
		_node = node;
		_alloc = node->getAllocator();

		_alloc->reserveChunks(160 * 144 * 4, 16); // Video buffers

		node->on<calls::SwapInstance>([&](const InstanceSwapDesc& d, SameBoyPlugPtr& other) {
			other = _instances[d.idx];
			_instances[d.idx] = d.instance;
			_instances[d.idx]->setSampleRate(_audioSettings.sampleRate);

			// TODO: Instantiate this in the UI thread and send with the SwapInstance message
			_audioBuffers[d.idx].data = std::make_shared<DataBuffer<float>>(_audioSettings.frameCount * 2);
			_audioBuffers[d.idx].frameCount = _audioSettings.frameCount;
		});

		node->on<calls::DuplicateInstance>([&](const InstanceDuplicateDesc& d, SameBoyPlugPtr& other) {
			other = _instances[d.targetIdx];
			_instances[d.targetIdx] = d.instance;
			_instances[d.targetIdx]->setSampleRate(_audioSettings.sampleRate);

			// TODO: Instantiate this in the UI thread and send with the SwapInstance message
			_audioBuffers[d.targetIdx].data = std::make_shared<DataBuffer<float>>(_audioSettings.frameCount * 2);
			_audioBuffers[d.targetIdx].frameCount = _audioSettings.frameCount;

			SameBoyPlugPtr source = _instances[d.sourceIdx];
			char* sourceState = new char[source->saveStateSize()];
			source->saveState(sourceState, source->saveStateSize());
			d.instance->loadState(sourceState, source->saveStateSize());
			delete[] sourceState;
		});

		node->on<calls::TakeInstance>([&](const InstanceIndex& idx, SameBoyPlugPtr& other) {
			other = _instances[idx];
			_instances.erase(_instances.begin() + idx);
			_instances.push_back(nullptr);

			for (size_t i = 0; i < MAX_INSTANCES; ++i) {
				if (!_instances[idx]) {
					_audioBuffers[idx].data = nullptr;
					_audioBuffers[idx].frameCount = 0;
				}
			}
		});

		node->on<calls::UpdateSettings>([&](const Project::Settings& settings) {
			_settings = settings;
		});

		node->on<calls::FetchState>([&](const FetchStateRequest& req, FetchStateResponse& state) {
			state.type = req.type;

			for (size_t i = 0; i < MAX_INSTANCES; ++i) {
				SameBoyPlugPtr inst = _instances[i];
				if (inst && req.buffers[i]) {
					state.buffers[i] = req.buffers[i];
					if (req.type == SaveStateType::Sram) {
						state.sizes[i] = inst->batterySize();
						inst->saveBattery(state.buffers[i]->data(), state.buffers[i]->size());
					} else if (req.type == SaveStateType::State) {
						state.sizes[i] = inst->saveStateSize();
						inst->saveState(state.buffers[i]->data(), state.buffers[i]->size());
					}
				}
			}
		});

		node->on<calls::PressButtons>([&](const ButtonStream<32>& presses) {
			for (size_t i = 0; i < presses.pressCount; ++i) {
				std::cout << ButtonTypes::toString((ButtonType)presses.presses[i].button) << "\t" << presses.presses[i].down << "\t" << presses.presses[i].duration << std::endl;
			}

			_instances[presses.idx]->pressButtons(presses.presses.data(), presses.pressCount);
		});
	}

	void process(float** outputs) {
		_node->pull();

		SameBoyPlug* plugs[MAX_INSTANCES] = { nullptr };
		SameBoyPlug* linkedPlugs[MAX_INSTANCES] = { nullptr };

		size_t totalPlugCount = 0;
		size_t plugCount = 0;
		size_t linkedPlugCount = 0;

		VideoStream video;

		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			SameBoyPlugPtr plugPtr = _instances[i];
			if (plugPtr) {
				SameBoyPlug* plug = plugPtr.get();
				plugs[i] = plug;

				VideoBuffer* v = &video.buffers[i];
				v->dimensions = plug->getDimensions();
				size_t dataSize = v->dimensions.w * v->dimensions.h * 4;

				if (_alloc->canAlloc<char>(dataSize)) {
					v->data = _alloc->allocArrayUnique<char>(dataSize);
				} else {
					std::cout << "Failed to alloc video buffer" << std::endl;
				}

				plug->setBuffers(v, &_audioBuffers[i]);

				if (!plug->gameLink()) {
					plugs[plugCount++] = plug;
				} else {
					linkedPlugs[linkedPlugCount++] = plug;
				}

				totalPlugCount++;

				/*if (transportChanged) {
					HandleTransportChange(plug, _transportRunning);
				}*/

				//MessageBus* bus = plug->messageBus();
				//_buttonQueue.update(bus, FramesToMs(frameCount));
				//GenerateMidiClock(plug, frameCount, transportChanged);
			}
		}

		for (size_t i = 0; i < plugCount; i++) {
			plugs[i]->update(_audioSettings.frameCount);
		}

		if (linkedPlugCount > 0) {
			linkedPlugs[0]->updateMultiple(linkedPlugs, linkedPlugCount, _audioSettings.frameCount);
		}

		int chanMultipler = 0;
		if (_audioSettings.channelCount == 8 && _settings.audioRouting != AudioChannelRouting::StereoMixDown) {
			chanMultipler = 2;
		}

		if (_node->canPush<calls::TransmitVideo>()) {
			_node->push<calls::TransmitVideo>(NodeTypes::Ui, video);
		}

		for (size_t i = 0; i < totalPlugCount; i++) {
			float* audio = _audioBuffers[i].data->data();
			for (size_t j = 0; j < _audioSettings.frameCount; j++) {
				outputs[i * chanMultipler][j] += audio[j * 2];
				outputs[i * chanMultipler + 1][j] += audio[j * 2 + 1];
			}
		}
	}
};
