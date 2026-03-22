#pragma once

#include <rfl.hpp>
#include <entt/entity/entity.hpp>

#include "core/CoreComponents.h"
#include "core/SystemTypes.h"
#include "core/HierarchyComponents.h"
#include "audio/MidiMessage.h"
#include "sameboy/SameBoyComponents.h"

#include "core/Effects.h"
#include "core/SampleCache.h"
#include "core/TaskBase.h"

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
		orb::Uint8Buffer state;
	};

	struct PadButtonEvent {
		entt::entity entity = entt::null;
		orb::PadButtonType button = orb::PadButtonType::COUNT;
		bool down = false;
	};;

	struct MidiEvent {
		entt::entity entity = entt::null;
		orb::MidiMessage message;
	};

	/*struct ButtonEvent {
		entt::entity entity;
		int button;
		bool down;
	};*/

	struct VideoFrameComponent {
		orb::ImagePtr frame;
	};

	struct SystemNameComponent {
		std::string name;
	};

	struct LsdjComponent;

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
