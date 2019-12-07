#include "model/RetroPlug.h"

#include <iostream>

RetroPlug::RetroPlug() { 
	
}

RetroPlug::~RetroPlug() {
}

void RetroPlug::setActive(InstanceIndex idx) {
	if (idx != _activeIdx) {
		_activeIdx = idx;
		_active = _plugs[idx];
		_dirtyUi = true;
	}
}

void RetroPlug::clear() {
	_projectPath.clear();
	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		_plugs[i] = nullptr;
	}
}

SameBoyPlugPtr RetroPlug::addInstance(EmulatorType emulatorType) {
	SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
	plug->setSampleRate(_sampleRate);

	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		if (!_plugs[i]) {
			_plugs[i] = plug;
			break;
		}
	}

	if (!_active) {
		_active = plug;
	}

	return plug;
}

void RetroPlug::removeInstance(size_t idx) {
	for (size_t i = idx; i < MAX_INSTANCES - 1; i++) {
		_plugs[i] = _plugs[i + 1];
	}

	_plugs[MAX_INSTANCES - 1] = nullptr;
}

size_t RetroPlug::instanceCount() const {
	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		if (!_plugs[i]) {
			return i;
		}
	}

	return 4;
}

void RetroPlug::getLinkTargets(std::vector<SameBoyPlugPtr>& targets, SameBoyPlugPtr ignore) {
	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		if (_plugs[i] && _plugs[i] != ignore && _plugs[i]->active() && _plugs[i]->gameLink()) {
			targets.push_back(_plugs[i]);
		}
	}
}

void RetroPlug::updateLinkTargets() {
	size_t count = instanceCount();
	std::vector<SameBoyPlugPtr> targets;
	for (size_t i = 0; i < count; i++) {
		auto target = _plugs[i];
		if (target->active() && target->gameLink()) {
			targets.clear();
			getLinkTargets(targets, target);
			target->setLinkTargets(targets);
		}
	}
}

void RetroPlug::setSampleRate(double sampleRate) {
	_sampleRate = sampleRate;

	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		SameBoyPlugPtr plugPtr = _plugs[i];
		if (plugPtr) {
			plugPtr->setSampleRate(sampleRate);
		}
	}
}
