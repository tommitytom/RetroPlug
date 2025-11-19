#include "SampleCache.h"

#define MA_LOG_LEVEL MA_LOG_LEVEL_VERBOSE
#include <miniaudio/miniaudio.h>

#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"

namespace rp {
	SampleData loadSample(std::string_view path) {
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 0);

		spdlog::info("Loading sample from {}", path);

		fw::Uint8Buffer fileData;
		if (!fw::FsUtil::readFile(std::string(path), fileData)) {
			spdlog::error("Failed to read file from disk: {}", path);
			return SampleData{};
		}

		ma_decoder decoder;
		ma_result result = ma_decoder_init_memory(fileData.data(), fileData.size(), &config, &decoder);

		if (result != MA_SUCCESS) {
			spdlog::error("Failed to decode file: {}", (int)result);
			return SampleData{};
		}

		size_t blockSize = 24000;
		size_t offset = 0;

		SampleData sample;
		sample.sampleRate = decoder.outputSampleRate;

		while (true) {
			sample.buffer.resize(sample.buffer.size() + blockSize);

			ma_uint64 framesRead;
			ma_decoder_read_pcm_frames(&decoder, sample.buffer.data() + offset, blockSize, &framesRead);
			offset += (size_t)framesRead;

			if (framesRead < blockSize) {
				sample.buffer.resize(offset);
				break;
			}
		}

		return sample;
	}

	void SampleCache::erase(const std::string& path) {
		std::lock_guard<std::mutex> lock(_mutex);
		_cache.erase(path);
	}

	SampleData* SampleCache::getOrLoadSample(const std::string& name) {
		// Return sample if sample exists already
		{
			std::lock_guard<std::mutex> lock(_mutex);
			auto it = _cache.find(name);
			if (it != _cache.end()) {
				return &it->second;
			}
		}

		// Load sample from disk and decode
		SampleData sample = loadSample(name);
		if (sample.buffer.empty()) {
			return nullptr;
		}

		// Check again to see if another thread loaded the sample in the meantime (rare)
		// If not, write the sample to the cache
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_cache[name] = std::move(sample);

			auto it = _cache.find(name);
			if (it != _cache.end()) {
				return &it->second;
			}

			return &_cache[name];
		}
	}
}
