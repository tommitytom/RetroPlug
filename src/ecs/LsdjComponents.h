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
	};

	struct LsdjEmptyKit { using Tag = rfl::Literal<"empty", "LsdjEmptyKit">; };

	struct LsdjRomKit {
		using Tag = rfl::Literal<"rom", "LsdjRomKit">;
		std::optional<std::string> name; // Optionally rename kit in rom
	};

	struct LsdjPatchedKit {
		using Tag = rfl::Literal<"patched", "LsdjPatchedKit">;
		std::string path;
		std::optional<std::string> name; // Optionally rename kit before patching
	};

	struct LsdjEditableKit {
		using Tag = rfl::Literal<"editable", "LsdjEditableKit">;
		std::string name;
		std::vector<LsdjEffect> effects;
		std::vector<LsdjSampleComponent> samples;
	};

	struct LsdjKitComponent {
		uint32 id = std::numeric_limits<uint32>::max();
		rfl::TaggedUnion<"type", LsdjEmptyKit, LsdjRomKit, LsdjPatchedKit, LsdjEditableKit> kit;
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
