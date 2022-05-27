#pragma once

#include <functional>
#include <thread>
#include <vector>
#include <moodycamel/blockingconcurrentqueue.h>
#include <moodycamel/lightweightsemaphore.h>

namespace rp {
	class ThreadPool {
	public:
		class Task {
		public:
			virtual void run() = 0;
		};

	private:
		using TaskDelegate = std::function<void()>;
		using TaskPair = std::pair<TaskDelegate, moodycamel::LightweightSemaphore*>;
		using TaskQueue = moodycamel::BlockingConcurrentQueue<TaskPair>;
		const TaskDelegate NO_OP = []() {};

		class TaskRunner {
		private:
			size_t _id = 0;
			std::atomic<bool> _active = true;
			std::atomic<bool> _cancelRequested = false;

		public:
			TaskRunner() {}
			TaskRunner(const TaskRunner& other) : _id(other._id) {}
			~TaskRunner() {}

			void setId(size_t id) {
				_id = id;
			}

			void run(TaskQueue& taskQueue, bool untilEmpty = false) {
				moodycamel::ConsumerToken consumerToken(taskQueue);

				while (!_cancelRequested) {
					TaskPair task;
					if (taskQueue.try_dequeue(consumerToken, task)) {
						_active = true;
						task.first();
						task.second->signal();
					} else if (untilEmpty) {
						break;
					} else {
						// No tasks are available so wait until there is one
						_active = false;
						if (taskQueue.wait_dequeue_timed(consumerToken, task, std::chrono::seconds(5))) {
							_active = true;
							task.first();
							task.second->signal();
						} else {
							_active = false;
						}
					}
				}

				_active = false;
			}

			bool isActive() const {
				return _active;
			}

			void cancel() {
				_cancelRequested = true;
			}
		};

		struct Worker {
			std::thread thread;
			TaskRunner runner;
		};

		std::vector<Worker> _workers;
		TaskQueue _taskQueue;
		moodycamel::LightweightSemaphore _sema;

	public:
		ThreadPool() {}
		~ThreadPool() { stop(); }

		std::vector<Worker>& getWorkers() {
			return _workers;
		}

		// Starts the pool with the requested number of threads/workers.
		// If no value is supplied, the CPU's core count will be used.
		void start(size_t workerCount = std::thread::hardware_concurrency()) {
			_workers.resize(workerCount);
			for (size_t i = 0; i < workerCount; ++i) {
				Worker& worker = _workers[i];
				worker.runner.setId(i + 1);
				worker.thread = std::thread([&]() { worker.runner.run(_taskQueue); });
			}
		}

		// Stops all threads that were created when start() was called.
		// Will block until all threads are terminated.
		void stop() {
			for (Worker& worker : _workers) {
				worker.runner.cancel();
				_taskQueue.enqueue({ NO_OP, &_sema });
			}

			for (Worker& worker : _workers) {
				worker.thread.join();
			}

			// Clear the task queue
			TaskPair task;
			while (_taskQueue.try_dequeue(task));

			_workers.clear();
		}

		// Enqueues a task to be run
		void enqueue(Task* task) {
			_taskQueue.enqueue({ [task]() { task->run(); }, &_sema });
		}

		// Enqueues a task to be run
		void enqueue(TaskDelegate&& taskFn) {
			_taskQueue.enqueue({ std::forward<TaskDelegate>(taskFn), &_sema });
		}

		// Waits until all workers are inactive.  Will also use this
		// thread to process tasks.
		void wait(std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(), std::chrono::milliseconds sleep = std::chrono::milliseconds(1)) {
			TaskRunner runner;
			runner.run(_taskQueue, true);

			// Wait for other workers to finish.
			// TODO: This is pretty gross, should use a semaphore for this.
			while (true) {
				bool active = false;
				for (Worker& worker : _workers) {
					if (worker.runner.isActive()) {
						active = true;
					}
				}

				if (!active) {
					break;
				}

				if (sleep != std::chrono::milliseconds::zero()) {
					std::this_thread::sleep_for(sleep);
				}
			}
		}

	private:
		static void runWorker(TaskRunner& runner, TaskQueue& queue) {
			runner.run(queue);
		}
	};
}
