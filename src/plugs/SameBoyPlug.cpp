#include "SameBoyPlug.h"
#include "util/String.h"
#include "Constants.h"
#include <filesystem>

#define MINIAUDIO_IMPLEMENTATION
#include "src/audio/miniaudio.h"

#include "resource.h"
#include "util/File.h"

const int FRAME_SIZE = 160 * 144 * 4;

int getGameboyModel(GameboyModel model) {
	switch (model) {
	case GameboyModel::Auto: return 0x205;
	case GameboyModel::DmgB: return 0x002;
	case GameboyModel::CgbC: return 0x203;
	case GameboyModel::CgbE: return 0x205;
	case GameboyModel::Agb: return 0x206;
	}
}

SameBoyPlug::SameBoyPlug() {
	// FIXME: Choose some better sizes here...
	_bus.audio.init(1024 * 1024);
	_bus.video.init(1024 * 1024);
	_bus.buttons.init(64);
	_bus.link.init(64);

	_library.load(IDR_RCDATA1);
	_library.get("sameboy_init", _symbols.sameboy_init);
	_library.get("sameboy_reset", _symbols.sameboy_reset);
	_library.get("sameboy_update", _symbols.sameboy_update);
	_library.get("sameboy_update_multiple", _symbols.sameboy_update_multiple);
	_library.get("sameboy_fetch_audio", _symbols.sameboy_fetch_audio);
	_library.get("sameboy_fetch_video", _symbols.sameboy_fetch_video);
	_library.get("sameboy_set_sample_rate", _symbols.sameboy_set_sample_rate);
	_library.get("sameboy_set_midi_bytes", _symbols.sameboy_set_midi_bytes);
	_library.get("sameboy_disable_rendering", _symbols.sameboy_disable_rendering);
	_library.get("sameboy_free", _symbols.sameboy_free);
	_library.get("sameboy_set_button", _symbols.sameboy_set_button);
	_library.get("sameboy_save_state_size", _symbols.sameboy_save_state_size);
	_library.get("sameboy_save_state", _symbols.sameboy_save_state);
	_library.get("sameboy_load_state", _symbols.sameboy_load_state);
	_library.get("sameboy_battery_size", _symbols.sameboy_battery_size);
	_library.get("sameboy_save_battery", _symbols.sameboy_save_battery);
	_library.get("sameboy_load_battery", _symbols.sameboy_load_battery);
	_library.get("sameboy_get_rom_name", _symbols.sameboy_get_rom_name);
	_library.get("sameboy_set_setting", _symbols.sameboy_set_setting);
	_library.get("sameboy_set_link_targets", _symbols.sameboy_set_link_targets);
}

void SameBoyPlug::init(const std::string& romPath, GameboyModel model, bool fastBoot) {
	_romPath = romPath;
	_model = model;

	void* instance = _symbols.sameboy_init(this, romPath.c_str(), getGameboyModel(model), fastBoot);
	const char* name = _symbols.sameboy_get_rom_name(instance);
	for (int i = 0; i < 16; i++) {
		if (name[i] == 0) {
			_romName = std::string(name, i);
		}
	}

	if (_romName.size() == 0) {
		_romName = std::string(name, 16);
	}

	size_t stateSize = _symbols.sameboy_save_state_size(instance);

	std::vector<char> saveData;
	_savePath = changeExt(romPath, ".sav");
	if (std::filesystem::exists(_savePath)) {
		readFile(_savePath, saveData);
		_symbols.sameboy_load_battery(instance, saveData.data(), saveData.size());
	}

	std::string romName = _romName;
	std::transform(romName.begin(), romName.end(), romName.begin(), ::tolower);
	_lsdj.found = romName.find("lsdj") == 0;
	if (_lsdj.found) {
		_lsdj.version = _romName.substr(5, 6);
		_lsdj.saveData = saveData;
	}

	_symbols.sameboy_set_sample_rate(instance, _sampleRate);

	_instance = instance;
}

void SameBoyPlug::reset(GameboyModel model, bool fast) {
	std::scoped_lock lock(_lock);
	_symbols.sameboy_reset(_instance, getGameboyModel(model), fast);
}

void SameBoyPlug::setSampleRate(double sampleRate) {
	_sampleRate = sampleRate;

	if (_instance) {
		_lock.lock();
		_symbols.sameboy_set_sample_rate(_instance, sampleRate);
		_lock.unlock();
	}
}

size_t SameBoyPlug::saveStateSize() {
	return _symbols.sameboy_save_state_size(_instance);
}

bool SameBoyPlug::saveBattery(const std::string& path) {
	std::vector<char> target;
	if (saveBattery(target)) {
		return writeFile(path, target);
	}

	return false;
}

bool SameBoyPlug::saveBattery(std::vector<char>& data) {
	size_t size = _symbols.sameboy_battery_size(_instance);
	if (size) {
		data.resize(size);
		std::scoped_lock lock(_lock);
		return _symbols.sameboy_save_battery(_instance, data.data(), data.size());
	}

	return false;
}

bool SameBoyPlug::loadBattery(const std::string& path, bool reset) {
	std::vector<char> data;
	if (!readFile(path, data)) {
		return false;
	}

	return loadBattery(data, reset);
}

bool SameBoyPlug::loadBattery(const std::vector<char>& data, bool reset) {
	std::scoped_lock lock(_lock);
	_symbols.sameboy_load_battery(_instance, data.data(), data.size());

	if (reset) {
		_symbols.sameboy_reset(_instance, getGameboyModel(_model), true);
	}

	return true;
}

void SameBoyPlug::saveState(char* target, size_t size) {
	std::scoped_lock lock(_lock);
	_symbols.sameboy_save_state(_instance, target, size);
}

void SameBoyPlug::loadState(const char* source, size_t size) {
	_lock.lock();
	_symbols.sameboy_load_state(_instance, source, size);
	_lock.unlock();
}

void SameBoyPlug::setSetting(const std::string& name, int value) {
	_lock.lock();
	_symbols.sameboy_set_setting(_instance, name.c_str(), value);
	_lock.unlock();
}

void SameBoyPlug::setLinkTargets(std::vector<SameBoyPlugPtr> linkTargets) {
	void* instances[MAX_INSTANCES];
	for (size_t i = 0; i < linkTargets.size(); i++) {
		instances[i] = linkTargets[i]->instance();
	}

	_lock.lock();
	_symbols.sameboy_set_link_targets(_instance, instances, linkTargets.size());
	_lock.unlock();
}

// This is called from the audio thread
void SameBoyPlug::sendMidiBytes(int offset, const char* bytes, size_t count) {
	_symbols.sameboy_set_midi_bytes(_instance, offset, bytes, count);
}

// This is called from the audio thread
void SameBoyPlug::update(size_t audioFrames) {
	updateButtons();
	_symbols.sameboy_update(_instance, audioFrames);
	updateAV(audioFrames);
}

void SameBoyPlug::updateMultiple(SameBoyPlug** plugs, size_t plugCount, size_t audioFrames) {
	void* instances[MAX_INSTANCES];
	for (size_t i = 0; i < plugCount; i++) {
		instances[i] = plugs[i]->instance();
		plugs[i]->updateButtons();
	}

	_symbols.sameboy_update_multiple(instances, plugCount, audioFrames);

	for (size_t i = 0; i < plugCount; i++) {
		plugs[i]->updateAV(audioFrames);
	}
}

void SameBoyPlug::disableRendering(bool disable) {
	std::scoped_lock lock(_lock);
	_symbols.sameboy_disable_rendering(_instance, disable);
}

void SameBoyPlug::updateButtons() {
	while (_bus.buttons.readAvailable()) {
		auto ev = _bus.buttons.readValue();
		_symbols.sameboy_set_button(_instance, ev.id, ev.down);
	}
}

void SameBoyPlug::updateAV(int audioFrames) {
	int16_t audio[1024 * 4]; // FIXME: Choose a realistic size for this...
	char video[FRAME_SIZE];

	int sampleCount = audioFrames * 2;

	_symbols.sameboy_fetch_audio(_instance, audio);
	size_t videoAvailable = _symbols.sameboy_fetch_video(_instance, (uint32_t*)video);

	if (videoAvailable > 0 && _bus.video.writeAvailable() >= FRAME_SIZE) {
		_bus.video.write(video, FRAME_SIZE);
	}

	// Convert to float
	float inputFloat[1024 * 4]; // FIXME: Choose a realistic size for this...
	ma_pcm_s16_to_f32(inputFloat, audio, sampleCount, ma_dither_mode_triangle);

	if (_bus.audio.writeAvailable() >= sampleCount) {
		_bus.audio.write(inputFloat, sampleCount);
	}
}

void SameBoyPlug::shutdown() {
	if (_instance) {
		_symbols.sameboy_free(_instance);
		_instance = nullptr;
	}
}
