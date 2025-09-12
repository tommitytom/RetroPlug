#pragma once

#include <rfl.hpp>
#include <entt/entity/entity.hpp>
#include "sameboy/semver.hpp"

#include "core/CoreComponents.h"
#include "core/SystemTypes.h"
#include "ecs/HierarchyComponents.h"
#include "sameboy/SameBoyComponents.h"

#include "lsdj/Ram.h"
#include "ecs/Effects.h"
#include "ecs/SampleCache.h"
#include "ecs/TaskBase.h"

namespace rp {
	struct SystemIoEvent {
		entt::entity entity;
		SystemIoPtr io;
	};

	struct ResetSystemEntityEvent {
		entt::entity entity;
	};

	struct FetchMemoryRequest {
		entt::entity entity;
		MemoryType type;
	};

	struct MemoryPatchEvent {
		entt::entity entity;
		std::vector<MemoryPatch> patches;
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
		std::unordered_set<rp::KitIndex> dirtyKits;
		std::unordered_map<rp::KitIndex, TaskPtr> patchTasks;
		std::optional<lsdj::MemoryOffsets> ramOffsets;
		std::unique_ptr<SampleCache> sampleCache = std::make_unique<SampleCache>();

		LsdjStateComponent(const LsdjStateComponent&) = delete;
		LsdjStateComponent& operator=(const LsdjStateComponent&) = delete;

		LsdjStateComponent(LsdjStateComponent&&) = default;
		LsdjStateComponent& operator=(LsdjStateComponent&&) = default;

		LsdjStateComponent() {}
		~LsdjStateComponent() = default;
	};

	using ReplicatedTypes = entt::type_list<
		SystemComponent,
		HierarchyComponent,
		LsdjComponent
	>;

	using SerializedTypes = entt::type_list<
		SystemLoadComponent,
		SameBoyComponent,
		LsdjComponent
	>;
}
