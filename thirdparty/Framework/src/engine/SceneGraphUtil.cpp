#include "SceneGraphUtil.h"

#include <spdlog/spdlog.h>

#include "RelationshipComponent.h"
#include "TransformComponent.h"

using namespace fw;

entt::entity SceneGraphUtil::getChildByName(entt::registry& registry, entt::entity entity, std::string_view name) {
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

void SceneGraphUtil::pushBack(entt::registry& registry, entt::entity entity, entt::entity parent) {
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

void SceneGraphUtil::pushFront(entt::registry& registry, entt::entity entity, entt::entity parent) {
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

void SceneGraphUtil::insertBefore(entt::registry& registry, entt::entity entity, entt::entity sibling) {
	assert(false); // NYI
}

void SceneGraphUtil::insertAfter(entt::registry& registry, entt::entity entity, entt::entity sibling) {
	assert(false); // NYI
}

PointF invertScale(const PointF& scale) {
	return PointF(1.0f / scale.x, 1.0f / scale.y);
}

void SceneGraphUtil::changeParent(entt::registry& registry, entt::entity entity, entt::entity parent) {
	const RelationshipComponent& source = registry.get<RelationshipComponent>(entity);
	const RelationshipComponent& target = registry.get<RelationshipComponent>(parent);
	const Mat3x3& targetTransform = registry.get<WorldTransformComponent>(parent).transform;
	const Mat3x3& sourceTransform = registry.get<WorldTransformComponent>(entity).transform;
	
	PointF targetPos = targetTransform.getTranslation() * invertScale(targetTransform.getScale());
	PointF sourceParentPos;

	if (source.parent != entt::null) {
		sourceParentPos = sourceTransform.getTranslation() * invertScale(sourceTransform.getScale());
	}

	SceneGraphUtil::remove(registry, entity);
	SceneGraphUtil::pushBack(registry, entity, parent);

	registry.get<TransformComponent>(entity).position += sourceParentPos - targetPos;
}

void destroyEntityRecursive(entt::registry& registry, entt::entity root) {
	SceneGraphUtil::each(registry, root, [&registry](entt::entity e) {
		destroyEntityRecursive(registry, e);
	});

	registry.destroy(root);
}

void SceneGraphUtil::remove(entt::registry& registry, entt::entity entity, bool destroyAtEndOfTick) {
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

	if (destroyAtEndOfTick) {
		//registry.emplace_or_replace<DestroyAtEndOfTickTag>(entity);
	}
}

void SceneGraphUtil::removeAllChildren(entt::registry& registry, entt::entity entity, bool destroyAtEndOfTick) {
	RelationshipComponent& rel = registry.get<RelationshipComponent>(entity);
}

void SceneGraphUtil::printHierarchy(entt::registry& reg, entt::entity e, std::string indent) {
	const NameComponent& name = reg.get<NameComponent>(e);
	const TransformComponent& trans = reg.get<TransformComponent>(e);

	spdlog::info(
		"{}{}: t = [{}, {}] s = [{}, {}] r = [{}]",
		indent,
		name.name,
		trans.position.x, trans.position.y,
		trans.scale.x, trans.scale.y,
		trans.rotation
	);

	SceneGraphUtil::each(reg, e, [&](entt::entity child) {
		printHierarchy(reg, child, indent + "\t");
	});
}

void SceneGraphUtil::updateWorldTransform(entt::registry& reg, entt::entity e) {
	const RelationshipComponent& rel = reg.get<RelationshipComponent>(e);
	const TransformComponent& trans = reg.get<TransformComponent>(e);
	WorldTransformComponent& world = reg.get<WorldTransformComponent>(e);

	if (rel.parent == entt::null) {
		world.transform = Mat3x3::trs(trans.position, trans.rotation, trans.scale);
	} else {
		const WorldTransformComponent& parentWorld = reg.get<WorldTransformComponent>(rel.parent);
		world.transform = Mat3x3::trs(trans.position, trans.rotation, trans.scale) * parentWorld.transform;
		//world.transform *= parentWorld.transform;
	}
}

void SceneGraphUtil::updateWorldTransforms(entt::registry& reg, entt::entity root) {
	SceneGraphUtil::eachRecursive(reg, root, [&](entt::entity e) {
		SceneGraphUtil::updateWorldTransform(reg, e);
	}, true);
}
