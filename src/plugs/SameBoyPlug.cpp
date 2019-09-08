#ifdef WIN32
#include <windows.h>
#endif

#include "SameBoyPlug.h"
#include "util/xstring.h"
#include "util/fs.h"
#include "Constants.h"
#include <fstream>

#define MINIAUDIO_IMPLEMENTATION
#include "src/audio/miniaudio.h"

#ifdef WIN32
#include "SameBoyWrapper.h"
#else
#define SAMEBOY_SYMBOLS.SAMEBOY_SYMBOLS
#include "thirdparty/SameBoy/retroplug/libretro.h"
#endif

#include "resource.h"
#include "util/File.h"
#include "lsdj/rom.h"
#include "lsdj/kit.h"
#include "lsdj/sample.h"

const int FRAME_SIZE = 160 * 144 * 4;

int getGameboyModel(GameboyModel model) {
	switch (model) {
	case GameboyModel::DmgB: return 0x002;
	case GameboyModel::CgbC: return 0x203;
	case GameboyModel::CgbE: return 0x205;
	case GameboyModel::Agb: return 0x206;
	case GameboyModel::Auto:
	default: return 0x205;
	}
}

SameBoyPlug::SameBoyPlug() {
	// FIXME: Choose some better sizes here...
	_bus.audio.init(1024 * 1024);
	_bus.video.init(1024 * 1024);
	_bus.buttons.init(64);
	_bus.link.init(64);
}

void SameBoyPlug::init(const tstring& romPath, GameboyModel model, bool fastBoot) {
	_romPath = romPath;
	_model = model;

	_romData.clear();
	if (!readFile(romPath, _romData)) {
		return;
	}

	void* instance = SAMEBOY_SYMBOLS.sameboy_init(this, (const char*)_romData.data(), _romData.size(), getGameboyModel(model), fastBoot);
	if (!instance) {
		return;
	}

	const char* name = SAMEBOY_SYMBOLS.sameboy_get_rom_name(instance);
	for (int i = 0; i < 16; i++) {
		if (name[i] == 0) {
			_romName = std::string(name, i);
		}
	}

	if (_romName.size() == 0) {
		_romName = std::string(name, 16);
	}

	std::vector<std::byte> saveData;
	if (_saveData.empty()) {
		_savePath = changeExt(romPath, T(".sav"));

		if (!fs::exists(_savePath)) {
			// If a .sav file with the same name as the rom does not exist, check to see if there is a 
			// single .sav in the same directory as the rom and load it.  If there are multiple .sav files
			// in the same directory, don't load any of them (as this could get confusing!).
			fs::path found;
			for (const auto& entry : fs::directory_iterator(fs::path(romPath).parent_path())) {
				if (entry.path().extension() == ".sav") {
					if (found.empty()) {
						found = entry.path();
					} else {
						found.clear();
						break;
					}
				}
			}

			if (!found.empty()) {
				_savePath = tstr(found.wstring());
			}
		}

		if (fs::exists(_savePath)) {
			readFile(_savePath, saveData);
			SAMEBOY_SYMBOLS.sameboy_load_battery(instance, (const char*)saveData.data(), saveData.size());
		}
	} else {
		switch (_saveType) {
		case SaveStateType::State:
			SAMEBOY_SYMBOLS.sameboy_load_state(instance, (const char*)_saveData.data(), _saveData.size());
			break;
		case SaveStateType::Sram:
			SAMEBOY_SYMBOLS.sameboy_load_battery(instance, (const char*)saveData.data(), saveData.size());
			break;
		}

		saveData = _saveData;
		_saveData.clear();
	}

	std::string romName = _romName;
	std::transform(romName.begin(), romName.end(), romName.begin(), ::tolower);
	_lsdj.found = romName.find("lsdj") == 0;
	if (_lsdj.found) {
		_lsdj.version = _romName.substr(5, 6);
		_lsdj.saveData = saveData;
		_lsdj.loadRom(_romData);
	}

	SAMEBOY_SYMBOLS.sameboy_set_sample_rate(instance, _sampleRate);

	_instance = instance;
}

void SameBoyPlug::reset(GameboyModel model, bool fast) {
	_model = model;
	_resetSamples = (int)(_sampleRate / 2);
	std::scoped_lock lock(_lock);
	SAMEBOY_SYMBOLS.sameboy_reset(_instance, getGameboyModel(model), fast);
}

void SameBoyPlug::setSampleRate(double sampleRate) {
	_sampleRate = sampleRate;

	if (_instance) {
		std::scoped_lock lock(_lock);
		SAMEBOY_SYMBOLS.sameboy_set_sample_rate(_instance, sampleRate);
	}
}

size_t SameBoyPlug::saveStateSize() {
	return SAMEBOY_SYMBOLS.sameboy_save_state_size(_instance);
}

size_t SameBoyPlug::batterySize() {
	return SAMEBOY_SYMBOLS.sameboy_battery_size(_instance);
}

bool SameBoyPlug::saveBattery(tstring path) {
	std::vector<std::byte> target;
	if (saveBattery(target)) {
		if (path.empty()) {
			path = _savePath;
		}

		if (writeFile(path, target)) {
			_savePath = path;
			return true;
		}
	}

	return false;
}

bool SameBoyPlug::saveBattery(std::vector<std::byte>& data) {
	size_t size = SAMEBOY_SYMBOLS.sameboy_battery_size(_instance);
	if (size) {
		data.resize(size);
		return saveBattery((std::byte*)data.data(), data.size());
	}

	return false;
}

bool SameBoyPlug::saveBattery(std::byte* data, size_t size) {
	std::scoped_lock lock(_lock);
	return SAMEBOY_SYMBOLS.sameboy_save_battery(_instance, (char*)data, size);
}

bool SameBoyPlug::loadBattery(const tstring& path, bool reset) {
	std::vector<std::byte> data;
	if (!readFile(path, data)) {
		return false;
	}

	_savePath = path;
	return loadBattery(data, reset);
}

bool SameBoyPlug::loadBattery(const std::vector<std::byte>& data, bool reset) {
	return loadBattery(data.data(), data.size(), reset);
}

bool SameBoyPlug::loadBattery(const std::byte* data, size_t size, bool reset) {
	if (_instance) {
		std::scoped_lock lock(_lock);
		SAMEBOY_SYMBOLS.sameboy_load_battery(_instance, (char*)data, size);

		if (reset) {
			_resetSamples = (int)(_sampleRate / 2);
			SAMEBOY_SYMBOLS.sameboy_reset(_instance, getGameboyModel(_model), true);
		}
	} else {
		_saveData.resize(size);
		_saveType = SaveStateType::Sram;
		memcpy(_saveData.data(), data, size);
	}

	return true;
}

bool SameBoyPlug::clearBattery(bool reset) {
	std::scoped_lock lock(_lock);
	size_t size = SAMEBOY_SYMBOLS.sameboy_battery_size(_instance);
	std::vector<std::byte> d(size);
	memset(d.data(), 0, size);
	SAMEBOY_SYMBOLS.sameboy_load_battery(_instance, (char*)d.data(), d.size());

	_savePath = T("");

	if (reset) {
		_resetSamples = (int)(_sampleRate / 2);
		SAMEBOY_SYMBOLS.sameboy_reset(_instance, getGameboyModel(_model), true);
	}

	return true;
}

void SameBoyPlug::saveState(std::vector<std::byte>& data) {
	size_t size = SAMEBOY_SYMBOLS.sameboy_save_state_size(_instance);
	if (size) {
		data.resize(size);
		return saveState((std::byte*)data.data(), data.size());
	}
}

void SameBoyPlug::saveState(std::byte* target, size_t size) {
	std::scoped_lock lock(_lock);
	SAMEBOY_SYMBOLS.sameboy_save_state(_instance, (char*)target, size);
}

void SameBoyPlug::loadState(const std::vector<std::byte>& data) {
	loadState(data.data(), data.size());
}

void SameBoyPlug::loadState(const std::byte* source, size_t size) {
	if (_instance) {
		std::scoped_lock lock(_lock);
		SAMEBOY_SYMBOLS.sameboy_load_state(_instance, (char*)source, size);
	} else {
		_saveData.resize(size);
		_saveType = SaveStateType::State;
		memcpy(_saveData.data(), source, size);
	}
}

void SameBoyPlug::setSetting(const std::string& name, int value) {
	std::scoped_lock lock(_lock);
	SAMEBOY_SYMBOLS.sameboy_set_setting(_instance, name.c_str(), value);
}

void SameBoyPlug::setLinkTargets(std::vector<SameBoyPlugPtr> linkTargets) {
	void* instances[MAX_INSTANCES];
	for (size_t i = 0; i < linkTargets.size(); i++) {
		instances[i] = linkTargets[i]->instance();
	}

	std::scoped_lock lock(_lock);
	SAMEBOY_SYMBOLS.sameboy_set_link_targets(_instance, instances, linkTargets.size());
}

void SameBoyPlug::sendKeyboardByte(int offset, char byte) {
	SAMEBOY_SYMBOLS.sameboy_send_serial_byte(_instance, offset, 0, 1);
	SAMEBOY_SYMBOLS.sameboy_send_serial_byte(_instance, offset, byte, 8);
	SAMEBOY_SYMBOLS.sameboy_send_serial_byte(_instance, offset, 0x01, 2);
}

void SameBoyPlug::sendSerialByte(int offset, char byte, size_t bitCount) {
	SAMEBOY_SYMBOLS.sameboy_send_serial_byte(_instance, offset, byte, bitCount);
}

// This is called from the audio thread
void SameBoyPlug::sendMidiBytes(int offset, const char* bytes, size_t count) {
	SAMEBOY_SYMBOLS.sameboy_set_midi_bytes(_instance, offset, bytes, count);
}

// This is called from the audio thread
void SameBoyPlug::update(size_t audioFrames) {
	updateButtons();
	SAMEBOY_SYMBOLS.sameboy_update(_instance, audioFrames);
	updateAV(audioFrames);
}

void SameBoyPlug::updateMultiple(SameBoyPlug** plugs, size_t plugCount, size_t audioFrames) {
	void* instances[MAX_INSTANCES];
	for (size_t i = 0; i < plugCount; i++) {
		instances[i] = plugs[i]->instance();
		plugs[i]->updateButtons();
	}

	SAMEBOY_SYMBOLS.sameboy_update_multiple(instances, plugCount, audioFrames);

	for (size_t i = 0; i < plugCount; i++) {
		plugs[i]->updateAV(audioFrames);
	}
}

void SameBoyPlug::disableRendering(bool disable) {
	std::scoped_lock lock(_lock);
	SAMEBOY_SYMBOLS.sameboy_disable_rendering(_instance, disable);
}

void SameBoyPlug::updateRom() {
	std::scoped_lock lock(_lock);
	SAMEBOY_SYMBOLS.sameboy_update_rom(_instance, (const char*)_romData.data(), _romData.size());
}

void SameBoyPlug::updateButtons() {
	while (_bus.buttons.readAvailable()) {
		auto ev = _bus.buttons.readValue();
		SAMEBOY_SYMBOLS.sameboy_set_button(_instance, ev.id, ev.down);
	}
}

void SameBoyPlug::updateAV(int audioFrames) {
	int16_t audio[1024 * 4]; // FIXME: Choose a realistic size for this...
	char video[FRAME_SIZE];

	int sampleCount = audioFrames * 2;

	SAMEBOY_SYMBOLS.sameboy_fetch_audio(_instance, audio);
	size_t videoAvailable = SAMEBOY_SYMBOLS.sameboy_fetch_video(_instance, (uint32_t*)video);

	if (videoAvailable > 0 && _bus.video.writeAvailable() >= FRAME_SIZE) {
		_bus.video.write(video, FRAME_SIZE);
	}

	if (_resetSamples <= 0) {
		// Convert to float
		float inputFloat[1024 * 4]; // FIXME: Choose a realistic size for this...
		ma_pcm_s16_to_f32(inputFloat, audio, sampleCount, ma_dither_mode_triangle);

		if (_bus.audio.writeAvailable() >= sampleCount) {
			_bus.audio.write(inputFloat, sampleCount);
		}
	} else {
		_resetSamples -= audioFrames;
	}
}

void SameBoyPlug::shutdown() {
	if (_instance) {
		SAMEBOY_SYMBOLS.sameboy_free(_instance);
		_instance = nullptr;
	}
}
