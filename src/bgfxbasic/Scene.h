#pragma once

#include <box2d/b2_world.h>
#include <box2d/b2_body.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_polygon_shape.h>
#include <entt/entity/registry.hpp>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include "platform/Types.h"
#include "RpMath.h"

#include "util/fs.h"

#include "shaders/fs_tex.h"
#include "shaders/vs_tex.h"

#include "BgfxCanvas.h"

#define WNDW_WIDTH 1600
#define WNDW_HEIGHT 900

namespace rp {
	struct SpriteComponent {

	};

	struct PhysicsWorldSingleton {
		b2World world;
	};

	struct PhysicsComponent {
		b2Body* body = nullptr;
		b2Fixture* fixture = nullptr;
	};

	namespace Box2dUtil {
		static PhysicsComponent& addRigidBody(entt::registry& reg, entt::entity e, const b2BodyDef& bodyDef, const b2Shape& shape, f32 density) {
			b2World& world = reg.ctx<PhysicsWorldSingleton>().world;

			PhysicsComponent& comp = reg.emplace<PhysicsComponent>(e, PhysicsComponent{
				.body = world.CreateBody(&bodyDef)
			});

			comp.fixture = comp.body->CreateFixture(&shape, density);

			return comp;
		}
	}

	class Scene {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		rp::engine::BgfxCanvas _canvas;

	public:
		Scene() = default;
		~Scene() = default;

		void init();

		void render();
	};
}
