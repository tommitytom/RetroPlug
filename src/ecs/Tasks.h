#pragma once

#include <entt/entity/registry.hpp>
#include <TaskScheduler.h>
#include "ecs/RetroPlugComponents.h"

namespace rp {
	class SampleCache;

	struct LoadSystemTask : enki::ITaskSet {
		entt::id_type systemType;
		entt::registry registry;
		entt::entity entity = entt::null;

		std::atomic<bool> completed = false;
		bool success = false;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;
	};

	struct LoadProjectTask : enki::ITaskSet {
		std::vector<std::filesystem::path> paths;
		entt::registry registry;

		std::atomic<bool> completed = false;
		bool success = false;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;
	};

	struct PatchKitTask : enki::ITaskSet {
		fw::Uint8Buffer kitData;
		LsdjKitComponent kitState;
		SampleCache* sampleCache = nullptr;

		std::atomic<bool> completed = false;
		bool success = false;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;
	};
}
