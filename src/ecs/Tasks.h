#pragma once

#include <entt/entity/registry.hpp>
#include <TaskScheduler.h>

#include "ecs/ProjectBuilder.h"

namespace rp {
	struct LoadSystemTask : enki::ITaskSet {
		entt::id_type systemType;
		entt::registry registry;
		entt::entity entity = entt::null;

		std::atomic<bool> completed = false;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override {
			ProjectBuilder::handleLoad(registry, entity, registry.get<SystemLoadComponent>(entity), systemType);
			completed = true;
		}
	};
}
