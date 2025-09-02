#pragma once

#include <entt/entity/registry.hpp>

#include "foundation/Event.h"
#include "foundation/DataBuffer.h"
#include "foundation/Replicator.h"
#include "ecs/RetroPlugComponents.h"

#include "core/SystemHook.h"
#include "sameboy/SameBoyComponents.h"
#include "foundation/FsUtil.h"

namespace rp {
	class RetroPlugProject {
	private:
		fw::EventNode _eventNode;
		entt::registry _registry;
		std::vector<std::unique_ptr<SystemHookBase>> _systemHooks;
		std::vector<std::unique_ptr<SystemHookBase>> _serviceHooks;

	public:
		RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId);
		~RetroPlugProject();

		template <typename T>
		entt::entity addSystem(SystemLoadComponent&& config, T&& component) {
			//assert(fw::Replicator::isReplicating<T>(fw::Replicator::getContext(_registry)));

			entt::id_type systemType = entt::type_id<T>().index();
			entt::entity entity = fw::Replicator::spawn(_registry);

			SystemLoadComponent& load = _registry.emplace<SystemLoadComponent>(entity, std::move(config));
			_registry.emplace<T>(entity, std::move(component));

			handleLoad(entity, load, systemType);

			return entity;
		}

		void removeSystem(entt::entity entity);

		void onUpdate(f32 deltaTime);

		void serialize(fw::Uint8Buffer& archive) const {}

		void deserialize(const fw::Uint8Buffer& archive) {}

		fw::EventNode& getEventNode() {
			return _eventNode;
		}

		entt::registry& getRegistry() {
			return _registry;
		}

		const entt::registry& getRegistry() const {
			return _registry;
		}

		const std::vector<std::unique_ptr<SystemHookBase>>& getSystemHooks() const {
			return _systemHooks;
		}

		const std::vector<std::unique_ptr<SystemHookBase>>& getServiceHooks() const {
			return _serviceHooks;
		}

	private:
		void handleLoad(entt::entity entity, SystemLoadComponent& load, entt::id_type systemType);
	};

	using RetroPlugProjectPtr = std::shared_ptr<RetroPlugProject>;
}
