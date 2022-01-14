#include "SampleLoaderUtil.h"

#define MA_LOG_LEVEL MA_LOG_LEVEL_VERBOSE
#include <miniaudio/miniaudio.h>

using namespace rp;

SampleLoaderUtil::SampleData SampleLoaderUtil::loadSample(std::string_view path) {
	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 0);

	ma_decoder decoder;
	ma_result result = ma_decoder_init_file(path.data(), &config, &decoder);

	if (result != MA_SUCCESS) {
		return SampleLoaderUtil::SampleData{};
	}

	size_t blockSize = 24000;
	size_t offset = 0;

	SampleLoaderUtil::SampleData sample;
	sample.buffer = std::make_shared<Float32Buffer>();

	while (true) {
		sample.buffer->resize(sample.buffer->size() + blockSize);

		ma_uint64 framesRead = ma_decoder_read_pcm_frames(&decoder, sample.buffer->data() + offset, blockSize);
		offset += (size_t)framesRead;

		if (framesRead < blockSize) {
			sample.buffer->resize(offset);
			break;
		}
	}

	return sample;
}
