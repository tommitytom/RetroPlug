#pragma once

#include <mutex>
#include <unordered_map>
#include "foundation/DataBuffer.h"

namespace rp {
	struct SampleData {
		fw::Float32Buffer buffer;
		uint32 sampleRate;
	};

	class SampleCache {
	private:
		std::unordered_map<std::string, SampleData> _cache;
		std::mutex _mutex;

	public:
		void addSample(const std::string& name, fw::Float32Buffer&& data, uint32 sampleRate) {
			std::lock_guard<std::mutex> lock(_mutex);
			_cache[name] = { std::move(data), sampleRate };
		}

		bool hasSample(const std::string& name) {
			std::lock_guard<std::mutex> lock(_mutex);
			return _cache.find(name) != _cache.end();
		}

		SampleData* getOrLoadSample(const std::string& name);
	};
}
