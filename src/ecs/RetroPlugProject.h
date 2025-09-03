#pragma once

#include <entt/entity/registry.hpp>

#include "foundation/Event.h"
#include "foundation/DataBuffer.h"
#include "foundation/Replicator.h"
#include "ecs/RetroPlugComponents.h"

#include "core/SystemHook.h"
#include "sameboy/SameBoyComponents.h"
#include "foundation/FsUtil.h"
#include "ecs/RetroPlugProjectContext.h"

namespace rp {
	class RetroPlugProject {
	private:
		fw::EventNode _eventNode;
		entt::registry _registry;

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
			_registry.emplace<SystemComponent>(entity, systemType);

			handleLoad(entity, load, systemType);

			fw::Uint8Buffer archive;
			serialize(archive);
			//deserialize(archive);

			return entity;
		}

		void removeSystem(entt::entity entity);

		void onUpdate(f32 deltaTime);

		void serialize(fw::Uint8Buffer& archive) const;

		void deserialize(const fw::Uint8Buffer& archive);

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

	private:
		void handleLoad(entt::entity entity, SystemLoadComponent& load, entt::id_type systemType);
	};

	using RetroPlugProjectPtr = std::shared_ptr<RetroPlugProject>;
}
