#pragma once

#include <entt/entity/registry.hpp>

#include "foundation/Event.h"

namespace rp {
	class RetroPlugProject {
	private:
		fw::EventNode _eventNode;
		entt::registry _registry;

	public:
		RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId);
		~RetroPlugProject();

		void onUpdate(f32 deltaTime);

		entt::registry& getRegistry() {
			return _registry;
		}

		const entt::registry& getRegistry() const {
			return _registry;
		}

		fw::EventNode& getEventNode() {
			return _eventNode;
		}
	};

	using RetroPlugProjectPtr = std::shared_ptr<RetroPlugProject>;
}
