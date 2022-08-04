#pragma once

#include <string_view>
#include <entt/entity/registry.hpp>
#include <spdlog/spdlog.h>

#include "RpMath.h"
#include "RelationshipComponent.h"

namespace rp::SceneGraphUtil {
	void pushBack(entt::registry& registry, entt::entity entity, entt::entity parent);

	void pushFront(entt::registry& registry, entt::entity entity, entt::entity parent);

	void insertBefore(entt::registry& registry, entt::entity entity, entt::entity sibling);

	void insertAfter(entt::registry& registry, entt::entity entity, entt::entity sibling);

	void changeParent(entt::registry& registry, entt::entity entity, entt::entity parent);

	// Removes the specified entity from the scene graph.
	// If destroyAtEndOfTick is set to true, the specified entity and all of its child nodes are 
	// deleted at the end of the tick.
	void remove(entt::registry& registry, entt::entity entity, bool destroyAtEndOfTick = false);

	void removeAllChildren(entt::registry& registry, entt::entity entity, bool destroyAtEndOfTick = false);

	static bool hasParent(entt::registry& registry, entt::entity entity) {
		return registry.get<RelationshipComponent>(entity).parent != entt::null;
	}

	static entt::entity getParent(entt::registry& registry, entt::entity entity) {
		return registry.get<RelationshipComponent>(entity).parent;
	}

	static entt::entity front(entt::registry& registry, entt::entity entity) {
		return registry.get<RelationshipComponent>(entity).first;
	}

	static entt::entity back(entt::registry& registry, entt::entity entity) {
		return registry.get<RelationshipComponent>(entity).last;
	}

	template <typename Func>
	void each(entt::registry& registry, entt::entity entity, Func f) {
		assert(entity != entt::null);
		assert(registry.valid(entity));

		const RelationshipComponent& rel = registry.get<RelationshipComponent>(entity);

		entt::entity curr = rel.first;
		while (curr != entt::null) {
			assert(registry.valid(curr)); // Entity must have been deleted, but not properly removed from scene graph

			// TODO: Discuss how this works, and how it handles the SceneGraph being manipulated mid iteration

			entt::entity next = registry.get<RelationshipComponent>(curr).next;
			f(curr);
			curr = next;
		}
	}

	template <typename Func>
	void eachRecursive(entt::registry& registry, entt::entity entity, Func f, bool includeRoot = false) {
		assert(entity != entt::null);
		assert(registry.valid(entity));

		if (includeRoot) {
			f(entity);
		}

		const RelationshipComponent& rel = registry.get<RelationshipComponent>(entity);

		entt::entity curr = rel.first;
		while (curr != entt::null) {
			assert(registry.valid(curr));

			// TODO: Discuss how this works, and how it handles the SceneGraph being manipulated mid iteration

			entt::entity next = registry.get<RelationshipComponent>(curr).next;

			f(curr);
			eachRecursive(registry, curr, f, false);

			curr = next;
		}
	}

	// Counts the number of child entities belonging to the specified entity
	static uint32 countChildren(entt::registry& registry, entt::entity entity) {
		uint32 count = 0;
		each(registry, entity, [&](entt::entity) { count++; });
		return count;
	}

	// Recursively counts the total number of child entities belonging to the specified entity
	static uint32 countChildrenRecursive(entt::registry& registry, entt::entity entity) {
		uint32 count = 0;
		eachRecursive(registry, entity, [&](entt::entity) { count++; });
		return count;
	}

	entt::entity getChildByName(entt::registry& registry, entt::entity entity, std::string_view name);

	void printHierarchy(entt::registry& reg, entt::entity e, std::string indent = "");

	void updateWorldTransform(entt::registry& reg, entt::entity entity);

	void updateWorldTransforms(entt::registry& reg, entt::entity root);	
}
