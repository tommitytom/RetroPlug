#pragma once

#include <spdlog/spdlog.h>
#include <entt/entity/registry.hpp>
#include "ecs/RetroPlugComponents.h"
#include "core/SystemHook.h"

namespace rp::ProjectBuilder {
	bool handleLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& config, entt::id_type systemType);

	template <typename T>
	bool addSystemWithConfig(entt::registry& registry, entt::entity entity, SystemLoadComponent&& config, const T& component) {
		if (entity == entt::null) {
			entity = registry.create();
		} else if (!registry.valid(entity)) {
			spdlog::error("Attempted to add system to invalid entity {}", (uint32)entity);
			return false;
		}

		const entt::id_type systemType = entt::type_id<T>().index();
		registry.emplace<T>(entity, component);
		registry.emplace<SystemComponent>(entity, systemType);
		SystemLoadComponent& load = registry.emplace<SystemLoadComponent>(entity, std::move(config));
		return handleLoad(registry, entity, load, systemType);
	}

	bool addSystem(entt::registry& registry, entt::entity entity, SystemLoadComponent&& config, entt::id_type systemType);

	bool loadFromFile(entt::registry& registry, std::filesystem::path path);

	bool loadFromPaths(entt::registry& registry, PathVector paths);

	bool saveToFile(entt::registry& registry, std::filesystem::path path);

	bool deserializeJson(entt::registry& registry, std::string_view str, const std::filesystem::path& rootPath);
}
