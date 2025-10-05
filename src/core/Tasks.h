#pragma once

#include <entt/entity/registry.hpp>

#include "core/RetroPlugComponents.h"
#include "core/TaskBase.h"

namespace rp {
	class SampleCache;

	struct LoadSystemTask : public TaskBase {
		entt::id_type systemType;
		entt::registry registry;
		entt::entity entity = entt::null;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;

		void finalize(entt::registry& targetRegistry) override;
	};

	struct LoadProjectTask : public TaskBase {
		std::vector<std::filesystem::path> paths;
		entt::registry registry;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;

		void finalize(entt::registry& targetRegistry) override;
	};
}
