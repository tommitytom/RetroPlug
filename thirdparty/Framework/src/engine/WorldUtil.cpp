#include "WorldUtil.h"

#include <entt/entity/registry.hpp>

#include "engine/RelationshipComponent.h"
#include "engine/SceneGraphUtil.h"
#include "foundation/ResourceManager.h"

using namespace fw;

entt::registry WorldUtil::createWorld(ResourceManager* resourceManager) {
	entt::registry reg;

	reg.ctx().emplace<WorldSingleton>(WorldSingleton{
		.rootEntity = WorldUtil::createEntity(reg, entt::null, TransformComponent()),
		.resourceManager = resourceManager
	});

	return reg;
}

entt::entity WorldUtil::createEntity(entt::registry& reg, entt::entity parent, const TransformComponent& trans) {
	entt::entity e = reg.create();

	reg.emplace<TransformComponent>(e, trans);
	reg.emplace<WorldTransformComponent>(e);
	reg.emplace<RelationshipComponent>(e);
	reg.emplace<WorldAabbComponent>(e);

	if (parent != entt::null) {
		SceneGraphUtil::pushBack(reg, e, parent);
	}

	return e;
}

entt::entity WorldUtil::createEntity(entt::registry& reg, const TransformComponent& trans) {
	return createEntity(reg, WorldUtil::getRootEntity(reg), trans);
}
