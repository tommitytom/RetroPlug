#pragma once

#include <filesystem>
#include <string>
#include <algorithm>

#include "plugs/SameBoyPlug.h"
#include "util/String.h"
#include "Constants.h"

enum class EmulatorType {
	SameBoy
};

class RetroPlug {
private:
	SameBoyPlugPtr _plugs[MAX_INSTANCES];
	double _sampleRate = 48000;

public:
	RetroPlug() {}
	~RetroPlug() {}

	SameBoyPlugPtr addInstance(EmulatorType emulatorType) {
		SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
		plug->setSampleRate(_sampleRate);

		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			if (!_plugs[i]) {
				_plugs[i] = plug;
				break;
			}
		}

		return plug;
	}

	void setSampleRate(double sampleRate) {
		_sampleRate = sampleRate;

		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			SameBoyPlugPtr plugPtr = _plugs[i];
			if (plugPtr) {
				plugPtr->setSampleRate(sampleRate);
			}
		}
	}

	SameBoyPlugPtr getPlug(size_t idx) {
		return _plugs[idx];
	}

	SameBoyPlugPtr* plugs() {
		return _plugs;
	}

	const SameBoyPlugPtr* plugs() const {
		return _plugs;
	}
};
