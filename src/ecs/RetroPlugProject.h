#pragma once

#include <chrono>
#include <entt/entity/registry.hpp>
#include <spdlog/spdlog.h>

#include "foundation/Event.h"
#include "foundation/DataBuffer.h"
#include "foundation/Replicator.h"
#include "ecs/RetroPlugComponents.h"

#include "core/SystemHook.h"
#include "sameboy/SameBoyComponents.h"
#include "foundation/FsUtil.h"
#include "ecs/RetroPlugProjectContext.h"
#include "ecs/LsdjController.h"
#include "ecs/ProjectBuilder.h"

#include "ecs/Tasks.h"

namespace rp {
	class RetroPlugProject {
	private:
		fw::EventNode _eventNode;
		entt::registry _registry;
		f32 _totalTime = 0.0f;

		bool _doPing = true;
		std::optional<std::chrono::high_resolution_clock::time_point> _lastPingTime;
		std::optional<std::chrono::high_resolution_clock::time_point> _lastPongTime;

		ProjectConfig _config;
		TaskManager _taskManager;
		std::vector<TaskId> _finishedTasks;

		bool _loading = false;

	public:
		RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId);
		~RetroPlugProject();

		void loadConfigs();

		bool loadFromFile(std::filesystem::path path);

		TaskId loadFromFileAsync(std::filesystem::path path) { return loadFromPathsAsync({ std::move(path) }); }

		TaskId loadFromPathsAsync(PathVector paths);

		bool saveToFile(std::filesystem::path path);

		bool loadFromPaths(PathVector paths);

		template <typename T>
		bool addSystem(SystemLoadComponent&& config, const T& component) {
			getContext().version++;
			entt::entity entity = fw::Replicator::spawn(_registry);
			if (ProjectBuilder::addSystemWithConfig<T>(_registry, entity, std::forward<SystemLoadComponent>(config), component)) {
				handleReplicate();
				return true;
			}

			return false;
		}

		template <typename T>
		TaskId addTask(std::unique_ptr<T>&& task) {
			return _registry.ctx().at<TaskManager>().addTask(std::move(task));
		}

		void getFinishedTasks(std::vector<TaskId>& outTasks) {
			outTasks = std::move(_finishedTasks);
			_finishedTasks.clear();
		}

		struct PendingLoadTag {};

		template <typename T>
		entt::entity addSystemAsync(SystemLoadComponent&& config, const T& component) {
			entt::entity entity = fw::Replicator::spawn(_registry);
			_registry.emplace<PendingLoadTag>(entity);

			std::unique_ptr<LoadSystemTask> loadTask = std::make_unique<LoadSystemTask>();
			loadTask->systemType = entt::type_id<T>().index();
			loadTask->entity = loadTask->registry.create(entity);
			loadTask->registry.ctx().emplace<HooksContext>(_registry.ctx().at<HooksContext>());
			loadTask->registry.ctx().emplace<ProjectPathContext>(_registry.ctx().at<ProjectPathContext>());
			loadTask->registry.emplace<T>(loadTask->entity, component);
			loadTask->registry.emplace<SystemComponent>(loadTask->entity, loadTask->systemType);
			loadTask->registry.emplace<SystemLoadComponent>(loadTask->entity, std::move(config));

			addTask(std::move(loadTask));

			return entity;
		}

		bool addSystem(SystemLoadComponent&& config);

		bool resetSystem(entt::entity system, bool remote);

		inline size_t getSystemCount() const {
			return _registry.view<SystemComponent>().size();
		}

		inline uint32 getVersion() const {
			return _registry.ctx().at<RetroPlugProjectContext>().version;
		}

		void subscribeToMemory(entt::entity entity, MemoryType type);

		void unsubscribeFromMemory(entt::entity entity, MemoryType type);

		void removeSystem(entt::entity entity);

		void reset();

		void onUpdate(f32 deltaTime);

		void serialize(fw::Uint8Buffer& archive, const std::filesystem::path& rootPath) const;

		std::string serializeJson(const std::filesystem::path& rootPath) const;

		bool deserialize(const fw::Uint8Buffer& archive, const std::filesystem::path& rootPath);

		bool deserializeJson(std::string_view str, const std::filesystem::path& rootPath);

		uint32 getMemoryVersion(entt::entity entity, MemoryType type) const;

		MemoryAccessor getSystemMemory(entt::entity entity, MemoryType type, AccessType access);

		std::vector<uint32> getSystemIds() const;

		std::string getProjectName() const;

		fw::EventNode& getEventNode() {
			return _eventNode;
		}

		entt::registry& getRegistry() {
			return _registry;
		}

		const entt::registry& getRegistry() const {
			return _registry;
		}

		RetroPlugProjectContext& getContext() {
			return _registry.ctx().at<RetroPlugProjectContext>();
		}

		const HooksContext& getHooksContext() const {
			return _registry.ctx().at<HooksContext>();
		}

		const RetroPlugProjectContext& getContext() const {
			return _registry.ctx().at<const RetroPlugProjectContext>();
		}

		LsdjController getLsdjController() {
			return LsdjController(_registry);
		}

		const std::filesystem::path& getMountPath() const {
			return _registry.ctx().at<ProjectPathContext>().mountPath;
		}

	private:
		void handleFetchTimers(f32 deltaTime);

		void handlePing();

		void handleAsyncTasks();

		void handleReplicate();
	};

	using RetroPlugProjectPtr = std::shared_ptr<RetroPlugProject>;
}
