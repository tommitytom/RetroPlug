#pragma once

#include <rfl.hpp>
#include <entt/entity/entity.hpp>
#include "sameboy/semver.hpp"

#include "core/CoreComponents.h"
#include "core/SystemTypes.h"
#include "ecs/HierarchyComponents.h"
#include "sameboy/SameBoyComponents.h"

#include "lsdj/Ram.h"

namespace rp {
	struct SystemIoEvent {
		entt::entity entity;
		SystemIoPtr io;
	};

	struct FetchMemoryRequest {
		entt::entity entity;
		MemoryType type;
	};

	struct FetchMemoryResponse {
		entt::entity entity;
		MemoryType type;
		fw::Uint8Buffer state;
	};

	struct ButtonEvent {
		entt::entity entity;
		int button;
		bool down;
	};;

	struct VideoFrameComponent {
		fw::ImagePtr frame;
	};

	struct SystemNameComponent {
		std::string name;
	};

	using KitIndex = uint32;

	struct LsdjKitEffect {
		int effectType = 0;
	};

	struct GainEffectComponent {
		float gain = 1.0f;
	};

	using LsdjEffect = std::variant<GainEffectComponent>;

	struct LsdjSampleComponent {
		int32 sampleId = -1;
		std::string name;
		std::string path;
		uint32 offset = 0;
		uint32 length = 0;
		std::vector<LsdjEffect> effects;

		rfl::Skip<fw::Uint8Buffer> data; // Populated on first load
	};

	struct LsdjKitComponent {
		int32 kitId = -1;
		std::string name;
		std::vector<LsdjEffect> effects;
		std::vector<LsdjSampleComponent> samples;
	};

	struct LsdjComponent {
		rfl::Skip<semver::version> version;
		rfl::Skip<std::optional<lsdj::MemoryOffsets>> ramOffsets;
		std::unordered_map<KitIndex, LsdjKitComponent> kits;
	};

	using ReplicatedTypes = entt::type_list<
		SystemComponent,
		HierarchyComponent,
		LsdjComponent,
		LsdjKitComponent,
		LsdjSampleComponent,
		LsdjKitEffect
	>;

	using SerializedTypes = entt::type_list<
		SystemLoadComponent,
		SameBoyComponent,
		LsdjComponent
	>;
}
