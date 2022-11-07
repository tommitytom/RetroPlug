#define MINIAUDIO_IMPLEMENTATION
#define MA_LOG_LEVEL MA_LOG_LEVEL_VERBOSE
#include <miniaudio/miniaudio.h>

#include "MiniAudioManager.h"

#include <spdlog/spdlog.h>

using namespace fw::audio;

static void callback(ma_device* pDevice, void* pOutput, const void* pInput, uint32 frameCount) {
	MiniAudioManager* m = (MiniAudioManager*)pDevice->pUserData;

	if (m->getProcessor()) {
		m->getProcessor()->onRender((f32*)pOutput, (const f32*)pInput, frameCount);
	}
}

struct MiniAudioManager::State {
	ma_context context;
	ma_device device;
	uint32 sampleRate;
};

MiniAudioManager::MiniAudioManager() {
	_state = new MiniAudioManager::State();
	_state->sampleRate = 48000;
}

MiniAudioManager::~MiniAudioManager() {
	stop();
	delete _state;
}

bool MiniAudioManager::setAudioDevice(uint32 idx) {
	ma_device_info* pPlaybackInfos;
	ma_uint32 playbackCount;
	ma_device_info* pCaptureInfos;
	ma_uint32 captureCount;
	if (ma_context_get_devices(&_state->context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
		spdlog::error("Failed to enumerate audio devices");
		return false;
	}

	if (idx >= playbackCount) {
		spdlog::error("Failed to set audio device: Device index is invalid");
		return false;
	}

	ma_device_stop(&_state->device);

	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.pDeviceID = &pPlaybackInfos[idx].id;
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = _state->sampleRate; // Set to 0 to use the device's native sample rate.
	config.dataCallback = callback;
	config.pUserData = this;

	if (ma_device_init(NULL, &config, &_state->device) != MA_SUCCESS) {
		spdlog::error("Failed to initialize audio device");
		return false;
	}

	ma_device_start(&_state->device);

	return true;
}

void MiniAudioManager::getDeviceNames(std::vector<std::string>& names) {
	ma_device_info* pPlaybackInfos;
	ma_uint32 playbackCount;
	ma_device_info* pCaptureInfos;
	ma_uint32 captureCount;
	if (ma_context_get_devices(&_state->context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
		spdlog::error("Failed to enumerate audio devices");
		return;
	}

	if (playbackCount == 0) {
		spdlog::warn("No audio devices were detected on this system");
	}

	for (ma_uint32 i = 0; i < playbackCount; ++i) {
		names.push_back(std::string(pPlaybackInfos[i].name));
	}
}

bool MiniAudioManager::loadFile(std::string_view path, std::vector<f32>& target) {
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

bool MiniAudioManager::start() {
	if (ma_context_init(NULL, 0, NULL, &_state->context) != MA_SUCCESS) {
		spdlog::error("Failed to create audio context");
		return false;
	}

	ma_device_info* pPlaybackInfos;
	ma_uint32 playbackCount;
	ma_device_info* pCaptureInfos;
	ma_uint32 captureCount;
	if (ma_context_get_devices(&_state->context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
		spdlog::error("Failed to enumerate audio devices");
		return false;
	}

	// Loop over each device info and do something with it. Here we just print the name with their index. You may want
	// to give the user the opportunity to choose which device they'd prefer.
	for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice += 1) {
		printf("%d - %s\n", iDevice, pPlaybackInfos[iDevice].name);
	}

	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	//config.playback.pDeviceID = &pPlaybackInfos[0].id;
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = _state->sampleRate; // Set to 0 to use the device's native sample rate.
	config.dataCallback = callback;
	config.pUserData = this;

	if (ma_device_init(NULL, &config, &_state->device) != MA_SUCCESS) {
		spdlog::error("Failed to initialize audio device");
		return false;
	}

	ma_device_start(&_state->device); // The device is sleeping by default so you'll need to start it manually.

	return true;
}

void MiniAudioManager::stop() {
	ma_device_uninit(&_state->device); // This will stop the device so no need to do that manually.
}

uint32 MiniAudioManager::getSampleRate() {
	return _state->sampleRate;
}
