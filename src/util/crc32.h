#pragma once

#include <stdint.h>
#include <vector>

namespace crc32 {
	uint32_t update(const void* buf, size_t len, uint32_t initial = 0);

	inline uint32_t update(const std::vector<std::byte>& data) {
		return update(data.data(), data.size());
	}
}
