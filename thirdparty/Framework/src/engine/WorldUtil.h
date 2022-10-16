#pragma once

#include <entt/fwd.hpp>
#include <entt/entity/registry.hpp>

#include "engine/TransformComponent.h"
#include "engine/WorldSingleton.h"

namespace fw {
	class ResourceManager;
	struct EventTag {};

	class World {
	private:
		entt::registry& _registry;

	public:
		World(entt::registry& registry) : _registry(registry) {}
	};

	namespace WorldUtil {
		entt::registry createWorld(ResourceManager* resourceManager = nullptr);

		entt::entity createEntity(entt::registry& reg, entt::entity parent, const TransformComponent& trans = TransformComponent());

		entt::entity createEntity(entt::registry& reg, const TransformComponent& trans = TransformComponent());

		inline ResourceManager* getResourceManager(const entt::registry& reg) {
			return reg.ctx().at<WorldSingleton>().resourceManager;
		}

		inline entt::entity getRootEntity(const entt::registry& reg) {
			return reg.ctx().at<WorldSingleton>().rootEntity;
		}

		template <typename T>
		inline bool hasComponent(const entt::registry& reg) {
			return reg.storage<T>().size() > 0;
		}

		template <typename T>
		entt::entity getUniqueEntity(const entt::registry& reg) {
			assert(reg.storage<T>().size() == 1);
			return reg.storage<T>().at(0);
		}

		template <typename T>
		T& getUniqueComponent(entt::registry& reg) {
			for (auto&& [v, c] : reg.view<T>().each()) { return c; }
			assert(false);
		}

		template <typename T>
		const T& getUniqueComponent(const entt::registry& reg) {
			for (auto&& [v, c] : reg.view<T>().each()) { return c; }
			assert(false);
		}

		template <typename T>
		inline void pushEvent(entt::registry& reg) {
			entt::entity e = reg.create();
			reg.emplace<T>(e);
			reg.emplace<EventTag>(e);
		}

		template <typename T>
		inline void pushEvent(entt::registry& reg, const T& ev = T()) {
			entt::entity e = reg.create();
			reg.emplace<T>(e, ev);
			reg.emplace<EventTag>(e);
		}

		inline void clearEvents(entt::registry& reg) {
			reg.view<EventTag>().each([&](entt::entity e) {
				reg.destroy(e);
			});
		}
	}
}
