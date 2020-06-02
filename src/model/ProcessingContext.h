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

	AudioBuffer _audioBuffers[MAX_SYSTEMS];
	GameboyButtonStream _buttonPresses[MAX_SYSTEMS];

	micromsg::Allocator* _alloc = nullptr;

public:
	ProcessingContext() {
		_instances.reserve(MAX_SYSTEMS);
		for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
			_instances.push_back(nullptr);
		}
	}

	~ProcessingContext() {
		for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
			if (_instances[i]) {
				_instances[i]->shutdown();
			}
		}

		_instances.clear();
	}

	void setNode(Node* node) { 
		_node = node; 
		_alloc = node->getAllocator();
	}

	SameBoyPlugPtr& getInstance(SystemIndex idx) { return _instances[idx]; }

	const Project::Settings& getSettings() const { return _settings; }

	void setSettings(const Project::Settings& settings) { _settings = settings; }

	void setSystemSettings(SystemIndex idx, SameBoySettings settings) {
		_instances[idx]->setSettings(settings);
		updateLinkTargets();
	}

	GameboyButtonStream* getButtonPresses(SystemIndex idx) {
		return &_buttonPresses[idx];
	}

	void fetchState(const FetchStateRequest& req, FetchStateResponse& state) {
		for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
			SameBoyPlugPtr inst = _instances[i];
			if (inst && req.srams[i]) {
				state.srams[i] = req.srams[i];
				state.states[i] = req.states[i];

				size_t sramSize = inst->batterySize();
				size_t stateSize = inst->saveStateSize();
				inst->saveBattery(state.srams[i]->data(), state.srams[i]->size());
				inst->saveState(state.states[i]->data(), state.states[i]->size());
				state.srams[i]->resize(sramSize);
				state.srams[i]->resize(stateSize);
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

	SameBoyPlugPtr swapInstance(SystemIndex idx, SameBoyPlugPtr instance) {
		SameBoyPlugPtr old = _instances[idx];
		_instances[idx] = instance;
		_instances[idx]->setSampleRate(_audioSettings.sampleRate);

		// TODO: Instantiate this in the UI thread and send with the SwapSystem message
		if (_audioSettings.frameCount > 0) {
			_audioBuffers[idx].data = std::make_shared<DataBuffer<float>>(_audioSettings.frameCount * 2);
			_audioBuffers[idx].frameCount = _audioSettings.frameCount;
		}

		return old;
	}

	SameBoyPlugPtr duplicateInstance(SystemIndex sourceIdx, SystemIndex targetIdx, SameBoyPlugPtr instance) {
		SameBoyPlugPtr old = swapInstance(targetIdx, instance);

		SameBoyPlugPtr source = _instances[sourceIdx];
		char* sourceState = new char[source->saveStateSize()];
		source->saveState(sourceState, source->saveStateSize());
		instance->loadState(sourceState, source->saveStateSize());
		delete[] sourceState;

		return old;
	}

	void resetInstance(SystemIndex idx, GameboyModel model) {
		SameBoyPlugPtr inst = _instances[idx];
		inst->reset(model, true);
	}

	SameBoyPlugPtr removeInstance(SystemIndex idx) {
		SameBoyPlugPtr old = _instances[idx];
		_instances.erase(_instances.begin() + idx);
		_instances.push_back(nullptr);

		for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
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
			for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
				if (_instances[i]) {
					_audioBuffers[i].data = std::make_shared<DataBuffer<float>>(frameCount * 2);
					_audioBuffers[i].frameCount = frameCount;
				}
			}
		}

		SameBoyPlug* plugs[MAX_SYSTEMS] = { nullptr };
		SameBoyPlug* linkedPlugs[MAX_SYSTEMS] = { nullptr };

		size_t totalPlugCount = 0;
		size_t plugCount = 0;
		size_t linkedPlugCount = 0;

		VideoStream video;

		for (size_t i = 0; i < MAX_SYSTEMS; i++) {
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

private:
	void getLinkTargets(std::vector<SameBoyPlugPtr>& targets, SameBoyPlugPtr ignore) {
		for (size_t i = 0; i < _instances.size(); i++) {
			const auto& settings = _instances[i]->getSettings();
			if (_instances[i] && _instances[i] != ignore && _instances[i]->active() && settings.gameLink) {
				targets.push_back(_instances[i]);
			}
		}
	}

	void updateLinkTargets() {
		std::vector<SameBoyPlugPtr> targets;
		for (size_t i = 0; i < _instances.size(); i++) {
			auto target = _instances[i];
			if (target && target->active() && target->getSettings().gameLink) {
				targets.clear();
				getLinkTargets(targets, target);
				target->setLinkTargets(targets);
			}
		}
	}

};
