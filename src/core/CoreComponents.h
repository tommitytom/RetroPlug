#pragma once

#include <string>
#include <unordered_map>
#include <rfl.hpp>
#include <entt/entity/registry.hpp>

#include "foundation/DataBuffer.h"
#include "core/MemoryAccessor.h"
#include "core/System.h"

namespace rp {
	struct SystemComponent {
		entt::id_type systemType = entt::null;
	};

	struct VersionedMemory {
		MemoryType type = MemoryType::MAX;
		fw::Uint8Buffer data;
		uint32 version = 0;
		f32 lastUpdate = 0;
		size_t subscriberCount = 0;
	};

	class CountdownTimer {
	private:
		f32 _time;

	public:
		CountdownTimer(f32 time) : _time(time) {}

		bool update(f32 deltaTime) {
			if (_time >= 0.0f) {
				_time -= deltaTime;
				if (_time < 0.0f) return true;
			} else {
				_time -= deltaTime;
			}

			return false;
		}

		void reset(f32 time) {
			_time = time;
		}

		f32 getTime() const {
			return _time;
		}
	};

	const f32 STATE_FETCH_INTERVAL = 1.0f;
	const f32 MEMORY_FETCH_INTERVAL = 1.0f / 60.0f;

	struct SystemStateComponent {
		std::string name;
		std::vector<VersionedMemory> memory;
		fw::Uint8Buffer state;
		f32 lastStateUpdate = 0.0f;
		CountdownTimer stateFetchTimer = STATE_FETCH_INTERVAL;
		CountdownTimer memoryFetchTimer = MEMORY_FETCH_INTERVAL;
		std::optional<SystemStateOffsets> stateOffsets;

		VersionedMemory* find(MemoryType type) {
			auto found = std::find_if(memory.begin(), memory.end(), [type](const VersionedMemory& mem) { return mem.type == type; });
			if (found != memory.end()) {
				return &(*found);
			}

			return nullptr;
		}

		const VersionedMemory* find(MemoryType type) const {
			auto found = std::find_if(memory.begin(), memory.end(), [type](const VersionedMemory& mem) { return mem.type == type; });
			if (found != memory.end()) {
				return &(*found);
			}

			return nullptr;
		}
	};

	struct SystemLoadEntry {
		std::string path;
		rfl::Skip<fw::Uint8Buffer> data;
	};

	struct SystemLoadComponent {
		std::map<std::string, SystemLoadEntry> entries;

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
