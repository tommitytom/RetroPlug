#include "HierarchyUtil.h"

#include <spdlog/spdlog.h>

#include "engine/RelationshipComponent.h"

namespace fw {
	entt::entity HierarchyUtil::getChildByName(entt::registry& registry, entt::entity entity, std::string_view name) {
		assert(entity != entt::null);

		const RelationshipComponent& rel = registry.get<RelationshipComponent>(entity);

		entt::entity curr = rel.first;
		while (curr != entt::null) {
			if (registry.get<NameComponent>(curr).name == name) {
				return curr;
			}

			curr = registry.get<RelationshipComponent>(curr).next;
		}

		return entt::null;
	}

	void HierarchyUtil::pushBack(entt::registry& registry, entt::entity entity, entt::entity parent) {
		assert(registry.all_of<RelationshipComponent>(parent));

		RelationshipComponent& rel = registry.get_or_emplace<RelationshipComponent>(entity);
		RelationshipComponent& parentRel = registry.get<RelationshipComponent>(parent);

		if (rel.parent != entt::null) {
			spdlog::warn("Entity {} is already parented to another entity.  Make sure it has been removed before changing parents", registry.get<NameComponent>(entity).name);
			remove(registry, entity);
		}

		rel.parent = parent;

		if (parentRel.first == entt::null) {
			parentRel.first = entity;
			parentRel.last = entity;
		} else {
			assert(parentRel.last != entt::null);
			rel.prev = parentRel.last;

			RelationshipComponent& siblingRel = registry.get<RelationshipComponent>(parentRel.last);
			siblingRel.next = entity;
			parentRel.last = entity;
		}
	}

	void HierarchyUtil::pushFront(entt::registry& registry, entt::entity entity, entt::entity parent) {
		assert(registry.try_get<RelationshipComponent>(parent));

		RelationshipComponent& rel = registry.get_or_emplace<RelationshipComponent>(entity);
		RelationshipComponent& parentRel = registry.get<RelationshipComponent>(parent);

		if (rel.parent != entt::null) {
			spdlog::warn("This entity is already parented to another entity.  Make sure it has been removed before changing parents");
			remove(registry, entity);
		}

		rel.parent = parent;

		if (parentRel.first == entt::null) {
			parentRel.first = entity;
			parentRel.last = entity;
		} else {
			assert(parentRel.last != entt::null);
			rel.next = parentRel.first;

			RelationshipComponent& siblingRel = registry.get<RelationshipComponent>(parentRel.first);
			siblingRel.prev = entity;
			parentRel.first = entity;
		}
	}

	void HierarchyUtil::insertBefore(entt::registry& registry, entt::entity entity, entt::entity sibling) {
		assert(false); // NYI
	}

	void HierarchyUtil::insertAfter(entt::registry& registry, entt::entity entity, entt::entity sibling) {
		assert(false); // NYI
	}

	PointF invertScale(const PointF& scale) {
		return PointF(1.0f / scale.x, 1.0f / scale.y);
	}

	void HierarchyUtil::changeParent(entt::registry& registry, entt::entity entity, entt::entity parent) {
		HierarchyUtil::remove(registry, entity);
		HierarchyUtil::pushBack(registry, entity, parent);
	}

	void destroyEntityRecursive(entt::registry& registry, entt::entity root) {
		HierarchyUtil::each(registry, root, [&registry](entt::entity e) {
			destroyEntityRecursive(registry, e);
		});

		registry.destroy(root);
	}

	void HierarchyUtil::remove(entt::registry& registry, entt::entity entity, bool destroyEntity) {
		RelationshipComponent& rel = registry.get<RelationshipComponent>(entity);

		assert(rel.next == entt::null || registry.valid(rel.next));
		assert(rel.prev == entt::null || registry.valid(rel.prev));

		if (rel.parent != entt::null) {
			assert(registry.valid(rel.parent));

			RelationshipComponent& parentRel = registry.get<RelationshipComponent>(rel.parent);

			if (parentRel.first == entity) {
				parentRel.first = rel.next;
			}

			if (parentRel.last == entity) {
				parentRel.last = rel.prev;
			}
		}

		if (rel.prev != entt::null) {
			RelationshipComponent& prevRel = registry.get<RelationshipComponent>(rel.prev);
			prevRel.next = rel.next;
		}

		if (rel.next != entt::null) {
			RelationshipComponent& nextRel = registry.get<RelationshipComponent>(rel.next);
			nextRel.prev = rel.prev;
		}

		rel.prev = entt::null;
		rel.next = entt::null;
		rel.parent = entt::null;

		if (destroyEntity) {
			destroyEntityRecursive(registry, entity);
		}
	}

	void HierarchyUtil::removeAllChildren(entt::registry& registry, entt::entity entity, bool destroyAtEndOfTick) {
		RelationshipComponent& rel = registry.get<RelationshipComponent>(entity);
	}

	void HierarchyUtil::printHierarchy(entt::registry& reg, entt::entity e, std::string indent) {
		const NameComponent& name = reg.get<NameComponent>(e);

		spdlog::info(
			"{}{}",
			indent,
			name.name
		);

		HierarchyUtil::each(reg, e, [&](entt::entity child) {
			printHierarchy(reg, child, indent + "\t");
		});
	}
}