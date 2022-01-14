#include "AudioLoaderUtil.h"

#include <miniaudio/miniaudio.h>
#include <spdlog/spdlog.h>

using namespace rp;

bool AudioLoaderUtil::load(std::string_view path, Float32Buffer& target) {
	ma_decoder decoder;
	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 44100);
	ma_result result = ma_decoder_init_file(path.data(), &config, &decoder);
	if (result != MA_SUCCESS) {
		return false; // An error occurred.
	}

	uint64 sampleCount = 0;
	ma_decoder_get_available_frames(&decoder, &sampleCount);
	target.resize((size_t)sampleCount);

	ma_uint64 framesRead = ma_decoder_read_pcm_frames(&decoder, target.data(), sampleCount);
	if (framesRead < sampleCount) {
		spdlog::info("Loaded file");
	}

	ma_decoder_uninit(&decoder);

	return true;
}
