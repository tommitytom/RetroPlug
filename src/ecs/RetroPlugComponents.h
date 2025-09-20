#pragma once

#include <rfl.hpp>
#include <entt/entity/entity.hpp>

#include "core/CoreComponents.h"
#include "core/SystemTypes.h"
#include "ecs/HierarchyComponents.h"
#include "sameboy/SameBoyComponents.h"
#include "ecs/LsdjComponents.h"

#include "lsdj/Ram.h"
#include "ecs/Effects.h"
#include "ecs/SampleCache.h"
#include "ecs/TaskBase.h"

namespace rp {
	struct ErrorComponent {
		std::string error;
	};

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

	struct PadButtonEvent {
		entt::entity entity = entt::null;
		fw::PadButtonType button = fw::PadButtonType::COUNT;
		bool down = false;
	};;

	/*struct ButtonEvent {
		entt::entity entity;
		int button;
		bool down;
	};*/

	struct VideoFrameComponent {
		fw::ImagePtr frame;
	};

	struct SystemNameComponent {
		std::string name;
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
