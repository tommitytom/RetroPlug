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
	Node* _node = nullptr;

	Project::Settings _settings;
	AudioSettings _audioSettings;

	AudioBuffer _audioBuffers[MAX_INSTANCES];
	GameboyButtonStream _buttonPresses[MAX_INSTANCES];

	micromsg::Allocator* _alloc = nullptr;

public:
	ProcessingContext() {
		_instances.reserve(MAX_INSTANCES);
		for (size_t i = 0; i < MAX_INSTANCES; ++i) {
			_instances.push_back(nullptr);
		}
	}
	~ProcessingContext() {}

	void setNode(Node* node) { 
		_node = node; 
		_alloc = node->getAllocator();
	}

	SameBoyPlugPtr& getInstance(InstanceIndex idx) { return _instances[idx]; }

	const Project::Settings& getSettings() const { return _settings; }

	void setSettings(const Project::Settings& settings) { _settings = settings; }

	GameboyButtonStream* getButtonPresses(InstanceIndex idx) {
		return &_buttonPresses[idx];
	}

	void fetchState(const FetchStateRequest& req, FetchStateResponse& state) {
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
	}

	void setAudioSettings(const AudioSettings& settings) {
		for (size_t i = 0; i < _instances.size(); ++i) {
			if (_instances[i]) {
				_instances[i]->setSampleRate(settings.sampleRate);
			}
		}

		_audioSettings = settings;
	}

	SameBoyPlugPtr swapInstance(InstanceIndex idx, SameBoyPlugPtr instance) {
		SameBoyPlugPtr old = _instances[idx];
		_instances[idx] = instance;
		_instances[idx]->setSampleRate(_audioSettings.sampleRate);

		// TODO: Instantiate this in the UI thread and send with the SwapInstance message
		if (_audioSettings.frameCount > 0) {
			_audioBuffers[idx].data = std::make_shared<DataBuffer<float>>(_audioSettings.frameCount * 2);
			_audioBuffers[idx].frameCount = _audioSettings.frameCount;
		}

		return old;
	}

	SameBoyPlugPtr duplicateInstance(InstanceIndex sourceIdx, InstanceIndex targetIdx, SameBoyPlugPtr instance) {
		SameBoyPlugPtr old = swapInstance(targetIdx, instance);

		SameBoyPlugPtr source = _instances[sourceIdx];
		char* sourceState = new char[source->saveStateSize()];
		source->saveState(sourceState, source->saveStateSize());
		instance->loadState(sourceState, source->saveStateSize());
		delete[] sourceState;

		return old;
	}

	SameBoyPlugPtr removeInstance(InstanceIndex idx) {
		SameBoyPlugPtr old = _instances[idx];
		_instances.erase(_instances.begin() + idx);
		_instances.push_back(nullptr);

		for (size_t i = 0; i < MAX_INSTANCES; ++i) {
			if (!_instances[i]) {
				_audioBuffers[i].data = nullptr;
				_audioBuffers[i].frameCount = 0;
			}
		}

		return old;
	}

	void process(float** outputs, size_t frameCount) {
		_node->pull();

		if (frameCount != _audioSettings.frameCount) {
			_audioSettings.frameCount = frameCount;
			for (size_t i = 0; i < MAX_INSTANCES; ++i) {
				if (_instances[i]) {
					_audioBuffers[i].data = std::make_shared<DataBuffer<float>>(frameCount * 2);
					_audioBuffers[i].frameCount = frameCount;
				}
			}
		}

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

				if (!plug->getSettings().gameLink) {
					plugs[plugCount++] = plug;
				} else {
					linkedPlugs[linkedPlugCount++] = plug;
				}

				totalPlugCount++;

				const ButtonStream<32>& d = _buttonPresses[i].data();
				if (d.pressCount > 0) {
					plug->pressButtons(d.presses.data(), d.pressCount);
					_buttonPresses[i].clear();
				}
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
