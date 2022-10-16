#pragma once

#include <entt/entity/registry.hpp>

#include "engine/CameraComponents.h"
#include "engine/SpriteComponent.h"
#include "engine/WorldUtil.h"

namespace fw::EngineUtil {
	static SpriteComponent& emplaceSprite(entt::registry& reg, entt::entity e, const TextureHandle& textureHandle, PointF pivot) {
		const Texture& texture = textureHandle.getResource();
		DimensionF dimensions = (DimensionF)texture.getDesc().dimensions;

		SpriteComponent& comp = reg.emplace_or_replace<SpriteComponent>(e, SpriteComponent{ 
			.uri = textureHandle.getUri(),
			.pivot = pivot
		});

		reg.emplace_or_replace<SpriteRenderComponent>(e, SpriteRenderComponent{
			.tile = {
				.handle = textureHandle,
				.dimensions = (DimensionF)texture.getDesc().dimensions,
				.name = textureHandle.getUri()
			},
			.renderArea = dimensions
		});

		return comp;
	}

	static SpriteComponent& emplaceSprite(entt::registry& reg, entt::entity e, std::string_view uri, PointF pivot) {
		TextureHandle texture = WorldUtil::getResourceManager(reg)->load<Texture>(uri);
		return emplaceSprite(reg, e, texture, pivot);
	}

	static entt::entity createSprite(entt::registry& reg, entt::entity parent, std::string_view uri, const TransformComponent& trans = TransformComponent()) {
		entt::entity e = WorldUtil::createEntity(reg, parent, trans);
		EngineUtil::emplaceSprite(reg, e, uri, PointF());
		return e;
	}

	static entt::entity createSprite(entt::registry& reg, std::string_view uri, const TransformComponent& trans = TransformComponent()) {
		return createSprite(reg, WorldUtil::getRootEntity(reg), uri, trans);
	}

	static entt::entity createOrthographicCamera(entt::registry& reg, entt::entity parent) {
		entt::entity e = WorldUtil::createEntity(reg, parent);
		reg.ctx().emplace<CameraSingleton>();
		reg.emplace<OrthographicCameraComponent>(e);
		reg.emplace<ActiveCameraTag>(e);
		return e;
	}

	static entt::entity createOrthographicCamera(entt::registry& reg) {
		return createOrthographicCamera(reg, WorldUtil::getRootEntity(reg));
	}

	template <typename Func>
	inline void visitComponents(const entt::registry& reg, const entt::entity e, Func&& f) {
		for (auto [id, pool] : reg.storage()) {
			if (pool.contains(e)) {
				f(pool.type());
			}
		}
	}
}
