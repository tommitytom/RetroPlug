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

	public:
		RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId);
		~RetroPlugProject();

		void loadConfigs();

		bool loadFromFile(std::filesystem::path path);

		bool saveToFile(std::filesystem::path path);

		bool loadFromPaths(PathVector paths);

		template <typename T>
		bool addSystem(SystemLoadComponent&& config, const T& component) {
			getContext().version++;
			entt::entity entity = fw::Replicator::spawn(_registry);
			return ProjectBuilder::addSystemWithConfig<T>(_registry, entity, std::forward<SystemLoadComponent>(config), component);
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

		const RetroPlugProjectContext& getContext() const {
			return _registry.ctx().at<const RetroPlugProjectContext>();
		}

		LsdjController getLsdjController() {
			return LsdjController(_registry);
		}

		const std::filesystem::path& getMountPath() const {
			return _registry.ctx().at<ProjectPathContext>().mountPath;
		}
	};

	using RetroPlugProjectPtr = std::shared_ptr<RetroPlugProject>;
}
