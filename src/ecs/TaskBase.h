#pragma once

#include <entt/entity/registry.hpp>
#include <TaskScheduler.h>

namespace rp {
	class TaskBase : public enki::ITaskSet {
	public:
		std::atomic<bool> completed = false;
		bool success = false;

		virtual void finalize(entt::registry& registry, entt::entity entity) {}
	};

	using TaskPtr = std::unique_ptr<TaskBase>;
}
