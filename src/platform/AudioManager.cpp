#define MINIAUDIO_IMPLEMENTATION
#define MA_LOG_LEVEL MA_LOG_LEVEL_VERBOSE
#include <miniaudio/miniaudio.h>

#ifndef RP_WEB

#include "AudioManager.h"

#include <spdlog/spdlog.h>

using namespace rp;

static void callback(ma_device* pDevice, void* pOutput, const void* pInput, uint32 frameCount) {
	AudioManager* m = (AudioManager*)pDevice->pUserData;

	if (m->getCallback()) {
		m->getCallback()((f32*)pOutput, (const f32*)pInput, frameCount);
	}
}

struct AudioManager::State {
	ma_device device;
	uint32 sampleRate;
};

AudioManager::AudioManager() {
	_state = new AudioManager::State();
	_state->sampleRate = 48000;
}

AudioManager::~AudioManager() {
	stop();
	delete _state;
}

bool AudioManager::loadFile(std::string_view path, std::vector<f32>& target) {
	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 11468);

	ma_decoder decoder;
	ma_result result = ma_decoder_init_file(path.data(), &config, &decoder);

	if (result != MA_SUCCESS) {
		return false;   // An error occurred.
	}

	size_t blockSize = 24000;
	size_t offset = 0;

	while (true) {
		target.resize(target.size() + blockSize);

		ma_uint64 framesRead = ma_decoder_read_pcm_frames(&decoder, target.data() + offset, blockSize);
		offset += (size_t)framesRead;

		if (framesRead < blockSize) {
			target.resize(offset);
			break;
		}
	}

	return true;

	/*ma_resampler_config config = ma_resampler_config_init(
	ma_format_s16,
	channels,
	sampleRateIn,
	sampleRateOut,
	ma_resample_algorithm_linear);

	ma_resampler resampler;
	ma_result result = ma_resampler_init(&config, &resampler);
	if (result != MA_SUCCESS) {
		// An error occurred...
	}*/
}

bool AudioManager::start() {
	ma_context context;
	if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
		// Error.
	}

	ma_device_info* pPlaybackInfos;
	ma_uint32 playbackCount;
	ma_device_info* pCaptureInfos;
	ma_uint32 captureCount;
	if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
		// Error.
	}

	// Loop over each device info and do something with it. Here we just print the name with their index. You may want
	// to give the user the opportunity to choose which device they'd prefer.
	for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice += 1) {
		printf("%d - %s\n", iDevice, pPlaybackInfos[iDevice].name);
	}

	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = _state->sampleRate; // Set to 0 to use the device's native sample rate.
	config.dataCallback = callback;
	config.pUserData = this;

	if (ma_device_init(NULL, &config, &_state->device) != MA_SUCCESS) {
		return false; // Failed to initialize the device.
	}

	ma_device_start(&_state->device); // The device is sleeping by default so you'll need to start it manually.

	return true;
}

void AudioManager::stop() {
	ma_device_uninit(&_state->device); // This will stop the device so no need to do that manually.
}

uint32 AudioManager::getSampleRate() {
	return _state->sampleRate;
}

#endif
