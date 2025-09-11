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
		bool success = false;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override {
			success = ProjectBuilder::handleLoad(registry, entity, registry.get<SystemLoadComponent>(entity), systemType);
			completed = true;
		}
	};

	struct LoadProjectTask : enki::ITaskSet {
		std::vector<std::filesystem::path> paths;
		entt::registry registry;

		std::atomic<bool> completed = false;
		bool success = false;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override {
			success = ProjectBuilder::loadFromPaths(registry, paths);
			completed = true;
		}
	};
}
