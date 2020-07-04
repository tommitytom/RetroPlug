#pragma once

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioRenderer* renderer = (AudioRenderer*)pDevice->pUserData;
	renderer->process(pOutput, frameCount);
}

class AudioRenderer {
private:
	ma_device _device;

public:
	AudioRenderer() {}
	~AudioRenderer() {}

	void init() {
		ma_device_config config = ma_device_config_init(ma_device_type_playback);
		config.playback.format   = ma_format_s16;
		config.playback.channels = 2;
		config.sampleRate        = 48000;
		config.dataCallback      = data_callback;
		config.pUserData         = this;

		if (ma_device_init(NULL, &config, &_device) != MA_SUCCESS) {
			printf("Failed to open playback device.\n");
			close();
			return;
		}

		if (ma_device_start(&_device) != MA_SUCCESS) {
			printf("Failed to start playback device.\n");
			close();
			return;
		}
	}

	void close() {
		ma_device_uninit(&_device);
	}

	void process(void* pOutput, ma_uint32 frameCount) {

	}
};
