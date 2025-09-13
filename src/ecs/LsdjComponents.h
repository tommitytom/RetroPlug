#pragma once

#include <optional>
#include <vector>

#include <rfl.hpp>
#include "sameboy/semver.hpp"

#include "foundation/Types.h"
#include "ecs/Effects.h"
#include "ecs/SampleCache.h"
#include "ecs/TaskBase.h"
#include "lsdj/Ram.h"

namespace rp {
	using KitIndex = uint32;
	constexpr KitIndex INVALID_KIT_INDEX = std::numeric_limits<KitIndex>::max();

	struct LsdjSampleComponent {
		std::string name;
		std::string path;
		uint32 offset = 0;
		uint32 length = 0;
		std::vector<LsdjEffect> effects;

		rfl::Skip<fw::Float32Buffer> data; // Populated on first load
	};

	struct LsdjKitComponent {
		uint32 id = std::numeric_limits<uint32>::max();
		std::string name;
		std::optional<std::string> path;
		std::optional<std::vector<LsdjEffect>> effects;
		std::optional<std::vector<LsdjSampleComponent>> samples;

		rfl::Skip<fw::Uint8Buffer> data; // Populated on first load
	};

	struct LsdjComponent {
		rfl::Skip<semver::version> version;
		std::vector<LsdjKitComponent> kits;
	};

	struct LsdjStateComponent {
		std::unordered_set<rp::KitIndex> dirtyKits; // Kits that need to be patched
		std::unordered_set<rp::KitIndex> patchingKits; // Currently being patched in worker thread
		std::optional<lsdj::MemoryOffsets> ramOffsets;
		std::array<uint32, 51> kitVersions;
		std::unique_ptr<SampleCache> sampleCache = std::make_unique<SampleCache>();

		LsdjStateComponent(const LsdjStateComponent&) = delete;
		LsdjStateComponent& operator=(const LsdjStateComponent&) = delete;

		LsdjStateComponent(LsdjStateComponent&&) = default;
		LsdjStateComponent& operator=(LsdjStateComponent&&) = default;

		LsdjStateComponent() { kitVersions.fill(1); }
		~LsdjStateComponent() = default;
	};
}
