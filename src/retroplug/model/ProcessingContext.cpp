#include "ProcessingContext.h"

#include <spdlog/spdlog.h>

ProcessingContext::ProcessingContext() {
	_systems.reserve(MAX_SYSTEMS);
	for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
		_systems.push_back(nullptr);
	}
}

ProcessingContext::~ProcessingContext() {
	for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
		if (_systems[i]) {
			_systems[i]->shutdown();
		}
	}

	_systems.clear();
}

void ProcessingContext::setSystemSettings(SystemIndex idx, SameBoySettings settings) {
	_systems[idx]->setSettings(settings);
	updateLinkTargets();
}

void ProcessingContext::setRenderingEnabled(bool enabled) {
	for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
		SameBoyPlugPtr inst = _systems[i];
		if (inst) {
			inst->disableRendering(!enabled);
		}
	}
}

void ProcessingContext::fetchState(const FetchStateRequest& req, FetchStateResponse& state) {
	for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
		SameBoyPlugPtr inst = _systems[i];
		if (inst) {
			if (req.srams[i]) {
				auto& buf = req.srams[i];
				size_t sramSize = inst->sramSize();
				buf->resize(sramSize);
				inst->saveSram(buf->data(), buf->size());
				state.srams[i] = buf;
			}

			if (req.states[i]) {
				auto& buf = req.states[i];
				size_t stateSize = inst->saveStateSize();
				buf->resize(stateSize);
				inst->saveState(buf->data(), buf->size());
				state.states[i] = buf;
			}
		}
	}
}

void ProcessingContext::setAudioSettings(const AudioSettings& settings) {
	for (size_t i = 0; i < _systems.size(); ++i) {
		if (_systems[i]) {
			_systems[i]->setSampleRate(settings.sampleRate);
		}
	}

	_audioSettings = settings;
}

SameBoyPlugPtr ProcessingContext::swapSystem(SystemIndex idx, SameBoyPlugPtr instance) {
	SameBoyPlugPtr old = _systems[idx];

	if (instance) {
		instance->setSampleRate(_audioSettings.sampleRate);
		_systems[idx] = instance;

		// TODO: Instantiate this in the UI thread and send with the SwapSystem message
		if (_audioSettings.frameCount > 0) {
			_audioBuffers[idx].data = std::make_shared<DataBuffer<float>>(_audioSettings.frameCount * 2);
			_audioBuffers[idx].frameCount = _audioSettings.frameCount;
		}
	} else {
		_audioBuffers[idx].data = nullptr;
		_audioBuffers[idx].frameCount = 0;
	}

	updateLinkTargets();

	return old;
}

SameBoyPlugPtr ProcessingContext::duplicateSystem(SystemIndex sourceIdx, SystemIndex targetIdx, SameBoyPlugPtr system) {
	SameBoyPlugPtr old = swapSystem(targetIdx, system);

	SameBoyPlugPtr source = _systems[sourceIdx];
	char* sourceState = new char[source->saveStateSize()];
	source->saveState(sourceState, source->saveStateSize());
	system->loadState(sourceState, source->saveStateSize());
	delete[] sourceState;

	return old;
}

void ProcessingContext::resetSystem(SystemIndex idx, GameboyModel model) {
	SameBoyPlugPtr inst = _systems[idx];
	inst->reset(model, true);
}

SameBoyPlugPtr ProcessingContext::removeSystem(SystemIndex idx) {
	SameBoyPlugPtr old = _systems[idx];
	_systems.erase(_systems.begin() + idx);
	_systems.push_back(nullptr);

	for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
		if (!_systems[i]) {
			_audioBuffers[i].data = nullptr;
			_audioBuffers[i].frameCount = 0;
		}
	}

	updateLinkTargets();

	return old;
}

void ProcessingContext::process(float** outputs, size_t frameCount) {
	_node->pull();

	if (frameCount != _audioSettings.frameCount) {
		_audioSettings.frameCount = frameCount;

		for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
			if (_systems[i]) {
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
		SameBoyPlugPtr plugPtr = _systems[i];

		if (plugPtr) {
			SameBoyPlug* plug = plugPtr.get();
			plugs[i] = plug;

			VideoBuffer* v = &video.buffers[i];
			v->dimensions = plug->getDimensions();
			size_t dataSize = (size_t)(v->dimensions.w * v->dimensions.h * 4);

			if (_alloc->canAlloc<char>(dataSize)) {
				v->data = _alloc->allocArrayUnique<char>(dataSize);
			} else {
				//spdlog::debug("Failed to alloc video buffer");
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

	bool hasVideo = false;
	for (const VideoBuffer& b : video.buffers) {
		if (b.hasData) {
			hasVideo = true;
			break;
		}
	}

	if (hasVideo && _node->canPush<calls::TransmitVideo>()) {
		_node->push<calls::TransmitVideo>(NodeTypes::Ui, std::move(video));
	}

	for (size_t i = 0; i < MAX_SYSTEMS; i++) {
		if (_audioBuffers[i].data) {
			float* audio = _audioBuffers[i].data->data();
			for (size_t j = 0; j < _audioSettings.frameCount; j++) {
				outputs[i * chanMultipler][j] += audio[j * 2];
				outputs[i * chanMultipler + 1][j] += audio[j * 2 + 1];
			}
		}
	}
}

void ProcessingContext::getLinkTargets(std::vector<SameBoyPlugPtr>& targets, SameBoyPlugPtr ignore) {
	for (size_t i = 0; i < _systems.size(); i++) {
		if (_systems[i]) {
			const auto& settings = _systems[i]->getSettings();

			if (_systems[i] != ignore && _systems[i]->active() && settings.gameLink) {
				targets.push_back(_systems[i]);
			}
		}
	}
}

void ProcessingContext::updateLinkTargets() {
	std::vector<SameBoyPlugPtr> targets;

	for (size_t i = 0; i < _systems.size(); i++) {
		auto target = _systems[i];

		if (target && target->active() && target->getSettings().gameLink) {
			targets.clear();
			getLinkTargets(targets, target);
			target->setLinkTargets(targets);
		}
	}
}
