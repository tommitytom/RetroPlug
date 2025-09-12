#pragma once

#include <entt/entity/registry.hpp>

#include "ecs/RetroPlugComponents.h"
#include "ecs/TaskBase.h"

namespace rp {
	class SampleCache;

	struct LoadSystemTask : public TaskBase {
		entt::id_type systemType;
		entt::registry registry;
		entt::entity entity = entt::null;


		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;

		void finalize(entt::registry& targetRegistry, entt::entity entity) override;
	};

	struct LoadProjectTask : TaskBase {
		std::vector<std::filesystem::path> paths;
		entt::registry registry;


		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;

		void finalize(entt::registry& targetRegistry, entt::entity entity) override;
	};

	struct PatchKitTask : TaskBase {
		fw::Uint8Buffer kitData;
		LsdjKitComponent kitState;
		SampleCache* sampleCache = nullptr;
		KitIndex kitIndex = INVALID_KIT_INDEX;
		entt::entity system = entt::null;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;

		void finalize(entt::registry& targetRegistry, entt::entity entity) override;
	};
}
