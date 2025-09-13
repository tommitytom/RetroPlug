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
	using TaskId = size_t;

	class TaskManager {
	private:
		using IndexedTask = std::pair<TaskId, TaskPtr>;

		enki::TaskScheduler _scheduler;
		std::vector<IndexedTask> _tasks;
		size_t _nextTaskId = 1;

	public:
		TaskId addTask(TaskPtr&& task) {
			const size_t id = _nextTaskId++;
			_tasks.push_back({ id, std::move(task) });
			_scheduler.AddTaskSetToPipe(_tasks.back().second.get());
			return id;
		}

		void resolveFinishedTasks(entt::registry& registry, entt::entity entity, std::vector<TaskId>& resolved) {
			auto it = _tasks.begin();
			while (it != _tasks.end()) {
				TaskBase* task = it->second.get();
				if (task->completed) {
					if (task->success) {
						task->finalize(registry, entity);
					}

					resolved.push_back(it->first);

					it = _tasks.erase(it);
				} else {
					++it;
				}
			}
		}

		enki::TaskScheduler& getScheduler() {
			return _scheduler;
		}
	};
}
