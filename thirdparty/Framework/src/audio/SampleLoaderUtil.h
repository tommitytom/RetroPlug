#pragma once

#include <string_view>
#include "foundation/DataBuffer.h"
#include "audio/AudioBuffer.h"

#define MA_LOG_LEVEL MA_LOG_LEVEL_VERBOSE
#include <miniaudio/miniaudio.h>

namespace fw::SampleLoaderUtil {
	template <const uint32 ChannelCount>
	bool loadSample(std::string_view path, InterleavedAudioBuffer<ChannelCount>& target) {
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, ChannelCount, 0);

		ma_decoder decoder;
		ma_result result = ma_decoder_init_file(path.data(), &config, &decoder);

		if (result != MA_SUCCESS) {
			return false;
		}

		const size_t blockSize = 24000;
		const size_t frameCount = blockSize / ChannelCount;
		size_t offset = 0;

		while (true) {
			target.resize(target.getFrameCount() + (uint32)frameCount);

			ma_uint64 framesRead = ma_decoder_read_pcm_frames(&decoder, target.getBuffer().data() + offset, frameCount);
			offset += (size_t)framesRead;

			if (framesRead < frameCount) {
				target.resize((uint32)offset);
				break;
			}
		}

		return true;
	}
}
