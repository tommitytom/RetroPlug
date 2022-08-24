#pragma once

#include <string>
#include <entt/entity/registry.hpp>

namespace fw {
	struct NameComponent {
		std::string name;
	};

	struct RelationshipComponent {
		entt::entity first = entt::null;
		entt::entity last = entt::null;
		entt::entity prev = entt::null;
		entt::entity next = entt::null;
		entt::entity parent = entt::null;
	};
}
