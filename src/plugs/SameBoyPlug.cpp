#include "SameBoyPlug.h"

#define RESAMPLER_IMPLEMENTATION
#include "src/audio/resampler.h"

#define MINIAUDIO_IMPLEMENTATION
#include "src/audio/miniaudio.h"

#include "resource.h"

const int FRAME_SIZE = 160 * 144 * 4;

void SameBoyPlug::init(const std::string & gamePath) {
	// FIXME: Choose some better sizes here...
	_bus.audio.init(1024 * 1024);
	_bus.video.init(1024 * 1024);
	_bus.buttons.init(64);
	_bus.link.init(64);

	_library.load(IDR_RCDATA1);
	_library.get("sameboy_init", _symbols.sameboy_init);
	_library.get("sameboy_update", _symbols.sameboy_update);
	_library.get("sameboy_audio_frames", _symbols.sameboy_audio_frames);
	_library.get("sameboy_set_sample_rate", _symbols.sameboy_set_sample_rate);
	_library.get("sameboy_set_midi_bytes", _symbols.sameboy_set_midi_bytes);
	_library.get("sameboy_fetch", _symbols.sameboy_fetch);
	_library.get("sameboy_free", _symbols.sameboy_free);
	_library.get("sameboy_set_button", _symbols.sameboy_set_button);
	_library.get("sameboy_save_state_size", _symbols.sameboy_save_state_size);
	_library.get("sameboy_save_state", _symbols.sameboy_save_state);
	_library.get("sameboy_load_state", _symbols.sameboy_load_state);
	_library.get("sameboy_load_battery", _symbols.sameboy_load_battery);
	_library.get("sameboy_get_rom_name", _symbols.sameboy_get_rom_name);

	_instance = _symbols.sameboy_init(this, gamePath.c_str());
	const char* name = _symbols.sameboy_get_rom_name(_instance);
	for (int i = 0; i < 16; i++) {
		if (name[i] == 0) {
			_romName = std::string(name, i);
		}
	}

	if (_romName.size() == 0) {
		_romName = std::string(name, 16);
	}

	_resampler = resampler_sinc_init();
}

void SameBoyPlug::setSampleRate(double sampleRate) {
	_symbols.sameboy_set_sample_rate(_instance, sampleRate);
}

void SameBoyPlug::sendMidiBytes(int offset, const char* bytes, size_t count) {
	if (_instance) {
		_symbols.sameboy_set_midi_bytes(_instance, offset, bytes, count);
	}
}

size_t SameBoyPlug::saveStateSize() {
	if (_instance) {
		return _symbols.sameboy_save_state_size(_instance);
	}

	return 0;
}

void SameBoyPlug::loadBattery(const std::string& path) {
	_symbols.sameboy_load_battery(_instance, path.c_str());
}

void SameBoyPlug::saveState(char* target, size_t size) {
	if (_instance) {
		_symbols.sameboy_save_state(_instance, target, size);
	}
}

void SameBoyPlug::loadState(const char* source, size_t size) {
	if (_instance) {
		_symbols.sameboy_load_state(_instance, source, size);
	}
}

void SameBoyPlug::update() {
	if (_instance) {
		while (_bus.buttons.readAvailable()) {
			auto ev = _bus.buttons.readValue();
			_symbols.sameboy_set_button(_instance, ev.id, ev.down);
		}

		_symbols.sameboy_update(_instance);

		int16_t audio[1024 * 32]; // FIXME: Choose a realistic size for this...
		char video[FRAME_SIZE];

		int frameCount = _symbols.sameboy_audio_frames(_instance);
		int sampleCount = frameCount * 2;

		_symbols.sameboy_fetch(_instance, audio, (uint32_t*)video);

		if (_bus.video.writeAvailable() >= FRAME_SIZE) {
			_bus.video.write(video, FRAME_SIZE);
		}

		// Convert to float
		float inputFloat[1024 * 32]; // FIXME: Choose a realistic size for this...
		//float outputFloat[1024 * 32];
		ma_pcm_s16_to_f32(inputFloat, audio, sampleCount, ma_dither_mode_triangle);

		float* outBuf = inputFloat;
		int outSize = sampleCount;
		/*struct resampler_data srcData = { 0 };
		if (_targetSampleRate != _timing.sampleRate) {
			srcData.input_frames = inFrames;
			srcData.ratio = _targetSampleRate / _timing.sampleRate;
			srcData.data_in = _inputFloat;
			srcData.data_out = _outputFloat;
			resampler_sinc_process(_resampler, &srcData);

			outBuf = _outputFloat;
			outSize = srcData.output_frames * 2;
		}*/

		if (_bus.audio.writeAvailable() >= outSize) {
			_bus.audio.write(outBuf, outSize);
		}
	}
}

void SameBoyPlug::shutdown() {
	if (_instance) {
		_symbols.sameboy_free(_instance);
		_instance = nullptr;
		free(_resampler);
	}
}
