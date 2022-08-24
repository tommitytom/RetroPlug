#pragma once

#include <box2d/b2_world.h>
#include <box2d/b2_body.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_circle_shape.h>
#include <box2d/b2_polygon_shape.h>

namespace fw {
	struct PhysicsWorldSingleton {
		std::unique_ptr<b2World> world;
		f32 meterSize = 64.0f;
		f32 delta = 0.0f;
	};

	struct PhysicsComponent {
		b2Body* body = nullptr;
		b2Fixture* fixture = nullptr;
	};
}