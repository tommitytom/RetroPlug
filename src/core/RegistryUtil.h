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

	template <typename Component>
	void moveComponent(entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) {
		Component* comp = sourceRegistry.try_get<Component>(sourceEntity);
		if (comp) {
			targetRegistry.emplace_or_replace<Component>(targetEntity, std::move(*comp));
		}
	}
}
