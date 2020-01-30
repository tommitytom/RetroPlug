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
#define SAMEBOY_SYMBOLS(symb) symb
#include "thirdparty/SameBoy/retroplug/libretro.h"
#endif

#include "resource.h"
#include "util/File.h"
#include "lsdj/rom.h"
#include "lsdj/kit.h"
#include "lsdj/sample.h"

//const int FRAME_SIZE = 160 * 144 * 4;

const int DEFAULT_GAMEBOY_MODEL = 0x205;

int getGameboyModelId(GameboyModel model) {
	switch (model) {
	case GameboyModel::DmgB: return 0x002;
	case GameboyModel::CgbC: return 0x203;
	case GameboyModel::CgbE: return 0x205;
	case GameboyModel::Agb: return 0x206;
	}

	return DEFAULT_GAMEBOY_MODEL;
}

SameBoyPlug::SameBoyPlug() {
	_dimensions.w = 160;
	_dimensions.h = 144;
}

void SameBoyPlug::pressButtons(const StreamButtonPress* presses, size_t pressCount) {
	double samplesPerMs = _sampleRate / 1000.0;
	for (size_t i = 0; i < pressCount; ++i) {
		int duration = (int)(samplesPerMs * presses[i].duration);
		SAMEBOY_SYMBOLS(sameboy_set_button)(_instance, duration, presses[i].button, presses[i].down);
	}
}

void SameBoyPlug::loadRom(const char* data, size_t size, bool fastBoot) {
	GameboyModel model = GameboyModel::Auto;
	_instance = SAMEBOY_SYMBOLS(sameboy_init)(this, data, size, getGameboyModelId(model), fastBoot);
	SAMEBOY_SYMBOLS(sameboy_disable_rendering)(_instance, false);
}

void SameBoyPlug::reset(GameboyModel model, bool fast) {
	_settings.model = model;
	_resetSamples = (int)(_sampleRate / 2);
	SAMEBOY_SYMBOLS(sameboy_reset)(_instance, getGameboyModelId(model), fast);
}

void SameBoyPlug::setSampleRate(double sampleRate) {
	_sampleRate = sampleRate;

	if (_instance) {
		SAMEBOY_SYMBOLS(sameboy_set_sample_rate)(_instance, sampleRate);
	}
}

size_t SameBoyPlug::saveStateSize() {
	return SAMEBOY_SYMBOLS(sameboy_save_state_size)(_instance);
}

size_t SameBoyPlug::batterySize() {
	return SAMEBOY_SYMBOLS(sameboy_battery_size)(_instance);
}

bool SameBoyPlug::saveBattery(char* data, size_t size) {
	return SAMEBOY_SYMBOLS(sameboy_save_battery)(_instance, (char*)data, size);
}

bool SameBoyPlug::loadBattery(const char* data, size_t size, bool reset) {
	if (_instance) {
		SAMEBOY_SYMBOLS(sameboy_load_battery)(_instance, (char*)data, size);

		if (reset) {
			_resetSamples = (int)(_sampleRate / 2);
			SAMEBOY_SYMBOLS(sameboy_reset)(_instance, getGameboyModelId(_settings.model), true);
		}
	}

	return true;
}

bool SameBoyPlug::clearBattery(bool reset) {
	size_t size = SAMEBOY_SYMBOLS(sameboy_battery_size)(_instance);
	std::vector<std::byte> d(size);
	memset(d.data(), 0, size);
	SAMEBOY_SYMBOLS(sameboy_load_battery)(_instance, (char*)d.data(), d.size());

	if (reset) {
		_resetSamples = (int)(_sampleRate / 2);
		SAMEBOY_SYMBOLS(sameboy_reset)(_instance, getGameboyModelId(_settings.model), true);
	}

	return true;
}

void SameBoyPlug::saveState(char* target, size_t size) {
	SAMEBOY_SYMBOLS(sameboy_save_state)(_instance, (char*)target, size);
}

void SameBoyPlug::loadState(const char* source, size_t size) {
	if (_instance) {
		SAMEBOY_SYMBOLS(sameboy_load_state)(_instance, (char*)source, size);
	}
}

void SameBoyPlug::setSetting(const std::string& name, int value) {
	SAMEBOY_SYMBOLS(sameboy_set_setting)(_instance, name.c_str(), value);
}

void SameBoyPlug::setLinkTargets(std::vector<SameBoyPlugPtr> linkTargets) {
	void* instances[MAX_INSTANCES];
	for (size_t i = 0; i < linkTargets.size(); i++) {
		instances[i] = linkTargets[i]->instance();
	}

	SAMEBOY_SYMBOLS(sameboy_set_link_targets)(_instance, instances, linkTargets.size());
}

void SameBoyPlug::sendKeyboardByte(int offset, char byte) {
	SAMEBOY_SYMBOLS(sameboy_send_serial_byte)(_instance, offset, 0, 1);
	SAMEBOY_SYMBOLS(sameboy_send_serial_byte)(_instance, offset, byte, 8);
	SAMEBOY_SYMBOLS(sameboy_send_serial_byte)(_instance, offset, 0x01, 2);
}

void SameBoyPlug::sendSerialByte(int offset, int byte, size_t bitCount) {
	SAMEBOY_SYMBOLS(sameboy_send_serial_byte)(_instance, offset, byte, bitCount);
}

// This is called from the audio thread
void SameBoyPlug::sendMidiBytes(int offset, const char* bytes, size_t count) {
	SAMEBOY_SYMBOLS(sameboy_set_midi_bytes)(_instance, offset, bytes, count);
}

// This is called from the audio thread
void SameBoyPlug::update(size_t audioFrames) {
	SAMEBOY_SYMBOLS(sameboy_update)(_instance, audioFrames);
	updateAV(audioFrames);
}

void SameBoyPlug::updateMultiple(SameBoyPlug** plugs, size_t plugCount, size_t audioFrames) {
	void* instances[MAX_INSTANCES];
	for (size_t i = 0; i < plugCount; i++) {
		instances[i] = plugs[i]->instance();
	}

	SAMEBOY_SYMBOLS(sameboy_update_multiple)(instances, plugCount, audioFrames);

	for (size_t i = 0; i < plugCount; i++) {
		plugs[i]->updateAV(audioFrames);
	}
}

void SameBoyPlug::disableRendering(bool disable) {
	SAMEBOY_SYMBOLS(sameboy_disable_rendering)(_instance, disable);
}

void SameBoyPlug::updateRom() {
	//SAMEBOY_SYMBOLS(sameboy_update_rom)(_instance, (const char*)_romData.data(), _romData.size());
}

void SameBoyPlug::updateAV(int audioFrames) {
	int16_t audio[1024 * 4]; // FIXME: Choose a realistic size for this...
	SAMEBOY_SYMBOLS(sameboy_fetch_audio)(_instance, audio);

	if (_resetSamples <= 0) {
		int sampleCount = audioFrames * 2;
		ma_pcm_s16_to_f32(_audioBuffer->data->data(), audio, sampleCount, ma_dither_mode_triangle);
	} else {
		_resetSamples -= audioFrames;
	}

	if (_videoBuffer->data.get()) {
		//memset(_videoBuffer->data.get(), 255, _videoBuffer->data.count());
		size_t videoAvailable = SAMEBOY_SYMBOLS(sameboy_fetch_video)(_instance, (uint32_t*)_videoBuffer->data.get());
		if (videoAvailable == 0) {
			_videoBuffer->data = nullptr;
		} else {
			assert(videoAvailable == _videoBuffer->data.count());
		}
	}
}

void SameBoyPlug::shutdown() {
	if (_instance) {
		SAMEBOY_SYMBOLS(sameboy_free)(_instance);
		_instance = nullptr;
	}
}
