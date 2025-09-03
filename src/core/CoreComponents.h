#pragma once

#include <string>
#include <unordered_map>
#include <rfl.hpp>
#include <entt/entity/registry.hpp>

#include "foundation/DataBuffer.h"

namespace rp {
	struct SystemLoadEntry {
		std::string path;
		rfl::Skip<fw::Uint8Buffer> data;
	};

	struct SystemComponent {
		entt::id_type systemType = entt::null;
	};

	struct SystemLoadComponent {
		std::unordered_map<std::string, SystemLoadEntry> entries;

		fw::Uint8Buffer* findData(const std::string& name) {
			auto found = entries.find(name);
			if (found != entries.end() && !found->second.data().empty()) {
				return &found->second.data();
			}

			return nullptr;
		}

		const fw::Uint8Buffer* findData(const std::string& name) const {
			auto found = entries.find(name);
			if (found != entries.end() && !found->second.data().empty()) {
				return &found->second.data();
			}

			return nullptr;
		}
	};
}
