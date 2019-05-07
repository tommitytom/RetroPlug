#pragma once

#include <filesystem>

#include "plugs/SameBoyPlug.h"
#include "util/String.h"
#include "roms/Lsdj.h"

enum class EmulatorType {
	SameBoy
};

class RetroPlug {
private:
	SameboyMin _sameboy;
	double _sampleRate = 48000;
	Lsdj _lsdj;

public:
	bool active() const { return _sameboy.active(); }

	Lsdj& lsdj() { return _lsdj; }

	void load(EmulatorType emulatorType, const std::string& romPath) {
		_sameboy.init(romPath);
		_sameboy.setSampleRate(_sampleRate);
		size_t stateSize = _sameboy.saveStateSize();

		std::string savPath = changeExt(romPath, ".sav");
		if (std::filesystem::exists(savPath)) {
			_sameboy.loadBattery(savPath);
		}

		_lsdj.found = _sameboy.romName().find("LSDj") == 0;
		if (_lsdj.found) {
			_lsdj.version = _sameboy.romName().substr(5, 6);
		}
	}

	void setSampleRate(double sampleRate) {
		_sampleRate = sampleRate;
		if (active()) {
			_sameboy.setSampleRate(sampleRate);
		}
	}

	void setButtonState(const ButtonEvent& ev) {
		_sameboy.messageBus()->buttons.writeValue(ev);
	}

	SameboyMin* plug() {
		return &_sameboy;
	}
};
