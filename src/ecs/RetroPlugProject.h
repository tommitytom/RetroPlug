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
#include "ecs/LsdjProject.h"

namespace rp {
	class RetroPlugProject {
	private:
		fw::EventNode _eventNode;
		entt::registry _registry;
		f32 _totalTime = 0.0f;

		bool _doPing = true;
		std::optional<std::chrono::high_resolution_clock::time_point> _lastPingTime;
		std::optional<std::chrono::high_resolution_clock::time_point> _lastPongTime;

	public:
		RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId);
		~RetroPlugProject();

		template <typename T>
		entt::entity addSystem(const SystemLoadComponent& config, const T& component) {
			//assert(fw::Replicator::isReplicating<T>(fw::Replicator::getContext(_registry)));

			entt::id_type systemType = entt::type_id<T>().index();
			entt::entity entity = fw::Replicator::spawn(_registry);

			SystemLoadComponent& load = _registry.emplace<SystemLoadComponent>(entity, config);
			_registry.emplace<T>(entity, component);

			handleLoad(entity, load, systemType);

			return entity;
		}

		entt::entity addSystem(const std::vector<std::string>& paths);

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

		void onUpdate(f32 deltaTime);

		void serialize(fw::Uint8Buffer& archive) const;

		std::string serializeToString() const;

		void deserialize(const fw::Uint8Buffer& archive);

		void deserializeFromString(std::string_view str);

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

	private:
		void handleLoad(entt::entity entity, SystemLoadComponent& load, entt::id_type systemType);
	};

	using RetroPlugProjectPtr = std::shared_ptr<RetroPlugProject>;
}
