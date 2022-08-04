#pragma once

#include <entt/entity/registry.hpp>

#include "RpMath.h"
#include "PhysicsComponents.h"

namespace rp::PhysicsUtil {
	static PointF convert(b2Vec2 v) {
		return PointF(v.x, v.y);
	}

	static b2Vec2 convert(PointF v) {
		return b2Vec2(v.x, v.y);
	}

	static PhysicsComponent& addBox(entt::registry& reg, entt::entity e, const RectF& area, f32 density = 0.0f) {
		PhysicsWorldSingleton& world = reg.ctx().at<PhysicsWorldSingleton>();

		b2BodyDef bodyDef;
		bodyDef.position.Set(area.getCenter().x / world.meterSize, area.getCenter().y / world.meterSize);

		if (density > 0) {
			bodyDef.type = b2_dynamicBody;
		}

		b2Body* body = world.world->CreateBody(&bodyDef);

		b2PolygonShape shape;
		shape.SetAsBox(area.w / 2 / world.meterSize, area.h / 2 / world.meterSize);

		b2FixtureDef fixtureDef;
		fixtureDef.shape = &shape;
		fixtureDef.density = density;
		fixtureDef.friction = 0.3f;

		b2Fixture* fixture = body->CreateFixture(&fixtureDef);

		return reg.emplace<PhysicsComponent>(e, PhysicsComponent{
			.body = body,
			.fixture = fixture
		});
	}

	static PhysicsComponent& addCircle(entt::registry& reg, entt::entity e, PointF position, f32 radius, f32 density = 0.0f) {
		PhysicsWorldSingleton& world = reg.ctx().at<PhysicsWorldSingleton>();

		b2BodyDef bodyDef;
		bodyDef.position.Set(position.x / world.meterSize, position.y / world.meterSize);

		if (density > 0) {
			bodyDef.type = b2_dynamicBody;
		}

		b2Body* body = world.world->CreateBody(&bodyDef);

		b2CircleShape shape;
		shape.m_radius = radius / world.meterSize;

		b2FixtureDef fixtureDef;
		fixtureDef.shape = &shape;
		fixtureDef.density = density;
		fixtureDef.friction = 0.3f;

		b2Fixture* fixture = body->CreateFixture(&fixtureDef);

		return reg.emplace<PhysicsComponent>(e, PhysicsComponent{
			.body = body,
			.fixture = fixture
		});
	}

	static PhysicsComponent& addRigidBody(entt::registry& reg, entt::entity e, const b2BodyDef& bodyDef, const b2Shape& shape, f32 density) {
		PhysicsWorldSingleton& world = reg.ctx().at<PhysicsWorldSingleton>();

		PhysicsComponent& comp = reg.emplace<PhysicsComponent>(e, PhysicsComponent {
			.body = world.world->CreateBody(&bodyDef)
		});

		comp.fixture = comp.body->CreateFixture(&shape, density);

		return comp;
	}
}
