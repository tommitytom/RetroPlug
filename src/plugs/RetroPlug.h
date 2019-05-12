#pragma once

#include <filesystem>
#include <string>

#include "plugs/SameBoyPlug.h"
#include "util/String.h"
#include "roms/Lsdj.h"

enum class EmulatorType {
	SameBoy
};

class RetroPlug {
private:
	SameBoyPlugPtr _sameboy;
	double _sampleRate = 48000;
	Lsdj _lsdj;
	std::string _savePath;
	std::string _romPath;

public:
	void load(EmulatorType emulatorType, const std::string& romPath) {
		SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();

		_romPath = romPath;

		plug->init(romPath);
		plug->setSampleRate(_sampleRate);
		size_t stateSize = plug->saveStateSize();

		_savePath = changeExt(romPath, ".sav");
		if (std::filesystem::exists(_savePath)) {
			plug->loadBattery(_savePath);
		}

		_lsdj.found = plug->romName().find("LSDj") == 0;
		if (_lsdj.found) {
			_lsdj.version = plug->romName().substr(5, 6);
		}

		_sameboy = plug;
	}

	void setSampleRate(double sampleRate) {
		_sampleRate = sampleRate;
		SameBoyPlugPtr plugPtr = _sameboy;
		if (plugPtr) {
			plugPtr->setSampleRate(sampleRate);
		}
	}

	void setButtonState(const ButtonEvent& ev) {
		SameBoyPlugPtr plugPtr = _sameboy;
		if (plugPtr) {
			plugPtr->messageBus()->buttons.writeValue(ev);
		}
	}

	void setSetting(const std::string& name, int value) {
		_sameboy->lock().lock();
		_sameboy->setSetting(name, value);
		_sameboy->lock().unlock();
	}

	void setOversample(int value) {
		_sameboy->lock().lock();
		_sameboy->setOversample(value);
		_sameboy->lock().unlock();
	}

	void saveBattery(const std::wstring& path) {
		SameBoyPlugPtr plugPtr = _sameboy;
		if (!plugPtr) {
			return;
		}

		std::string p = ws2s(path);
		if (p.size() == 0) {
			p = _savePath;
		}

		plugPtr->lock().lock();
		plugPtr->saveBattery(p);
		plugPtr->lock().unlock();
	}

	void loadBattery(const std::wstring& path) {
		SameBoyPlugPtr plugPtr = _sameboy;
		if (!plugPtr) {
			return;
		}

		std::string p = ws2s(path);
		plugPtr->lock().lock();
		plugPtr->loadBattery(p);
		plugPtr->lock().unlock();
	}

	SameBoyPlugPtr plug() {
		return _sameboy;
	}

	SameBoyPlugPtr plug() const {
		return _sameboy;
	}

	Lsdj& lsdj() { 
		return _lsdj; 
	}

	const std::string& savePath() const {
		return _savePath;
	}

	const std::string& romPath() const {
		return _romPath;
	}
};
