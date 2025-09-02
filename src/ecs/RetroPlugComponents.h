#pragma once

#include <entt/entity/entity.hpp>
#include "core/SystemTypes.h"
#include "ecs/HierarchyComponents.h"
#include "sameboy/SameBoyComponents.h"
#include "core/CoreComponents.h"

namespace rp {
	struct SystemIoEvent {
		entt::entity entity;
		SystemIoPtr io;
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

	struct LsdjComponent {
		int version;
	};

	struct LsdjKitComponent {
		int32 kitId = -1;
		std::string name;
	};

	struct LsdjSampleComponent {
		int32 sampleId = -1;
		std::string name;
		std::string path;
		uint32 offset = 0;
		uint32 length = 0;
	};

	struct LsdjKitEffect {
		int effectType = 0;
	};

	using ReplicatedTypes = entt::type_list<
		HierarchyComponent,
		LsdjComponent,
		LsdjKitComponent,
		LsdjSampleComponent,
		LsdjKitEffect
	>;
}
