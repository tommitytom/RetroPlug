#pragma once

#include <entt/entity/registry.hpp>

namespace rp::RegistryUtil {
	template <typename Component>
	Component* tryGet(entt::registry& registry, entt::entity entity) {
		if (!registry.valid(entity)) {
			return nullptr;
		}

		return registry.try_get<Component>(entity);
	}
}
