#pragma once

#include <spdlog/spdlog.h>
#include <entt/entity/registry.hpp>
#include <TaskScheduler.h>

namespace rp {
	class TaskBase : public enki::ITaskSet {
	private:
		std::atomic<bool> _completed = false;
		bool _success = false;
		std::string _error;

	protected:
		void setSuccess(bool success = true) {
			_success = success;
			_completed = true;
		}

		void setError(std::string&& error) {
			_success = false;
			_error = std::move(error);
			spdlog::error("Task error: {}", _error);
			_completed = true;
		}

	public:
		//TaskBase() = default;
		//virtual ~TaskBase() {}

		bool hasFinished() const {
			return _completed.load();
		}

		bool getSuccess() const {
			return _success;
		}

		const std::string& getError() const {
			return _error;
		}

		virtual void setup(const entt::registry& registry) {}

		virtual void finalize(entt::registry& registry) {}
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

		void resolveFinishedTasks(entt::registry& registry, std::vector<TaskId>& resolved) {
			auto it = _tasks.begin();
			while (it != _tasks.end()) {
				TaskBase* task = it->second.get();
				if (task->hasFinished()) {
					if (task->getSuccess()) {
						task->finalize(registry);
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
