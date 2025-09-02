#pragma once

#include <string>
#include <unordered_map>

#include "foundation/DataBuffer.h"

namespace rp {
	struct SystemLoadEntry {
		std::string path;
		fw::Uint8Buffer data;
	};

	struct SystemLoadComponent {
		std::unordered_map<std::string, SystemLoadEntry> entries;

		fw::Uint8Buffer* findData(const std::string& name) {
			auto found = entries.find(name);
			if (found != entries.end() && !found->second.data.empty()) {
				return &found->second.data;
			}

			return nullptr;
		}
	};
}
