#pragma once

#include <entt/entity/registry.hpp>

#include "HierarchyComponents.h"

#include <queue>

namespace rp::HierarchyUtil {
	/**
	 * Iterates over all direct children of a parent entity.
	 *
	 * @param registry The ECS registry containing the entities
	 * @param parent The parent entity to iterate children of (must have HierarchyComponent)
	 * @param func Callback function called for each child entity
	 */
	void each(entt::registry& registry, entt::entity parent, const std::function<void(entt::entity)>& func) {
		assert(registry.all_of<HierarchyComponent>(parent));

		const auto& parentHierarchy = registry.get<HierarchyComponent>(parent);
		entt::entity current = parentHierarchy.first;

		while (current != entt::null) {
			func(current);
			const auto& currentHierarchy = registry.get<HierarchyComponent>(current);
			current = currentHierarchy.next;
		}
	}

	/**
	 * Adds a child entity to a parent entity in the hierarchy.
	 * Creates HierarchyComponent for both entities if they don't exist.
	 * The child is appended to the end of the parent's child list.
	 *
	 * @param registry The ECS registry containing the entities
	 * @param parent The parent entity to add the child to
	 * @param child The child entity to be added
	 */
	void addChild(entt::registry& registry, entt::entity parent, entt::entity child) {
		// Ensure both entities have hierarchy components
		if (!registry.all_of<HierarchyComponent>(parent)) {
			registry.emplace<HierarchyComponent>(parent);
		}
		if (!registry.all_of<HierarchyComponent>(child)) {
			registry.emplace<HierarchyComponent>(child);
		}

		auto& parentHierarchy = registry.get<HierarchyComponent>(parent);
		auto& childHierarchy = registry.get<HierarchyComponent>(child);

		// Set child's parent
		childHierarchy.parent = parent;

		// If parent has no children, make this the first child
		if (parentHierarchy.first == entt::null) {
			parentHierarchy.first = child;
			childHierarchy.prev = entt::null;
			childHierarchy.next = entt::null;
		} else {
			// Find the last child and append to the end
			entt::entity lastChild = parentHierarchy.first;
			while (true) {
				auto& lastChildHierarchy = registry.get<HierarchyComponent>(lastChild);
				if (lastChildHierarchy.next == entt::null) {
					break;
				}
				lastChild = lastChildHierarchy.next;
			}

			// Link the new child at the end
			auto& lastChildHierarchy = registry.get<HierarchyComponent>(lastChild);
			lastChildHierarchy.next = child;
			childHierarchy.prev = lastChild;
			childHierarchy.next = entt::null;
		}
	}

	/**
	 * Removes a child entity from its parent in the hierarchy.
	 * Updates the linked list pointers to maintain continuity.
	 * The child's parent/sibling relationships are cleared, but the child entity
	 * and its HierarchyComponent remain in the registry.
	 *
	 * @param registry The ECS registry containing the entities
	 * @param child The child entity to remove (must have HierarchyComponent)
	 */
	void removeChild(entt::registry& registry, entt::entity child) {
		assert(registry.all_of<HierarchyComponent>(child));

		auto& childHierarchy = registry.get<HierarchyComponent>(child);
		entt::entity parent = childHierarchy.parent;

		// If child has no parent, nothing to remove from
		if (parent == entt::null) {
			return;
		}

		assert(registry.all_of<HierarchyComponent>(parent));
		auto& parentHierarchy = registry.get<HierarchyComponent>(parent);

		// Update sibling links
		if (childHierarchy.prev != entt::null) {
			auto& prevHierarchy = registry.get<HierarchyComponent>(childHierarchy.prev);
			prevHierarchy.next = childHierarchy.next;
		}

		if (childHierarchy.next != entt::null) {
			auto& nextHierarchy = registry.get<HierarchyComponent>(childHierarchy.next);
			nextHierarchy.prev = childHierarchy.prev;
		}

		// If this was the first child, update parent's first pointer
		if (parentHierarchy.first == child) {
			parentHierarchy.first = childHierarchy.next;
		}

		// Clear the child's hierarchy relationships
		childHierarchy.parent = entt::null;
		childHierarchy.prev = entt::null;
		childHierarchy.next = entt::null;
	}

	/**
	 * Completely removes an entity and all its descendants from the hierarchy.
	 * This is a destructive operation that recursively removes the entire subtree.
	 * The entity is removed from its parent's child list, all descendants are
	 * recursively removed, and the HierarchyComponent is removed from the entity.
	 *
	 * @param registry The ECS registry containing the entities
	 * @param entity The entity to remove along with its entire subtree
	 */
	void removeFromHierarchy(entt::registry& registry, entt::entity entity) {
		if (!registry.all_of<HierarchyComponent>(entity)) {
			return;
		}

		// Remove this entity from its parent
		removeChild(registry, entity);

		// Remove all children and recursively clean up their subtrees
		each(registry, entity, [&](entt::entity child) {
			removeFromHierarchy(registry, child);
		});

		// Remove the hierarchy component entirely
		registry.remove<HierarchyComponent>(entity);
	}

	/**
	 * Recursively iterates over all descendants using breadth-first traversal.
	 * Processes all children at the current level before moving to grandchildren.
	 * This is generally preferred for most use cases as it provides better cache
	 * locality and more predictable processing order.
	 *
	 * Order: child1, child2, child3, grandchild1, grandchild2, etc.
	 *
	 * @param registry The ECS registry containing the entities
	 * @param parent The parent entity to start iteration from (must have HierarchyComponent)
	 * @param func Callback function called for each descendant entity
	 */
	void eachRecursive(entt::registry& registry, entt::entity parent, const std::function<void(entt::entity)>& func) {
		assert(registry.all_of<HierarchyComponent>(parent));

		std::queue<entt::entity> queue;
		queue.push(parent);

		while (!queue.empty()) {
			entt::entity current = queue.front();
			queue.pop();

			// Process all direct children of current entity
			each(registry, current, [&](entt::entity child) {
				func(child);
				queue.push(child);  // Add child to queue for later processing
			});
		}
	}

	/**
	 * Recursively iterates over all descendants using depth-first traversal.
	 * Processes each child's entire subtree before moving to the next sibling.
	 * Useful for operations like cleanup, serialization, or transform propagation
	 * where you need to complete entire branches before proceeding.
	 *
	 * Order: child1, grandchild1, great-grandchild1, grandchild2, child2, etc.
	 *
	 * @param registry The ECS registry containing the entities
	 * @param parent The parent entity to start iteration from (must have HierarchyComponent)
	 * @param func Callback function called for each descendant entity
	 */
	void eachRecursiveDepthFirst(entt::registry& registry, entt::entity parent, const std::function<void(entt::entity)>& func) {
		assert(registry.all_of<HierarchyComponent>(parent));

		each(registry, parent, [&](entt::entity child) {
			func(child);
			// Recursively process this child's subtree before moving to next sibling
			eachRecursiveDepthFirst(registry, child, func);
		});
	}
}

