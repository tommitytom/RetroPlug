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

#include "shaders/fs_debug.h"
#include "shaders/vs_debug.h"

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
			b2World& world = reg.ctx().at<PhysicsWorldSingleton>().world;

			PhysicsComponent& comp = reg.emplace<PhysicsComponent>(e, PhysicsComponent {
				.body = world.CreateBody(&bodyDef)
			});

			comp.fixture = comp.body->CreateFixture(&shape, density);

			return comp;
		}
	}

	struct PosColorVertex
	{
		float x;
		float y;
		float z;
		uint32_t abgr;
	};

	static PosColorVertex cubeVertices[] =
	{
		{-1.0f,  1.0f,  1.0f, 0xff000000 },
		{ 1.0f,  1.0f,  1.0f, 0xff0000ff },
		{-1.0f, -1.0f,  1.0f, 0xff00ff00 },
		{ 1.0f, -1.0f,  1.0f, 0xff00ffff },
		{-1.0f,  1.0f, -1.0f, 0xffff0000 },
		{ 1.0f,  1.0f, -1.0f, 0xffff00ff },
		{-1.0f, -1.0f, -1.0f, 0xffffff00 },
		{ 1.0f, -1.0f, -1.0f, 0xffffffff },
	};

	static const uint16_t cubeTriList[] =
	{
		0, 1, 2,
		1, 3, 2,
		4, 6, 5,
		5, 6, 7,
		0, 2, 4,
		4, 2, 6,
		1, 5, 3,
		5, 7, 3,
		0, 4, 1,
		4, 5, 1,
		2, 3, 6,
		6, 3, 7,
	};

	/*static bgfx::ShaderHandle loadShader(const uint8_t* data, size_t size, const char* name = nullptr) {
		const bgfx::Memory* mem = bgfx::makeRef(data, size);
		bgfx::ShaderHandle handle = bgfx::createShader(mem);

		if (name) {
			bgfx::setName(handle, name);
		}
		
		return handle;
	}*/

	class Scene {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		bgfx::VertexBufferHandle _vert;
		bgfx::IndexBufferHandle _ind;
		bgfx::ProgramHandle _prog;

	public:
		Scene() = default;
		~Scene() = default;

		void init();

		void render();
	};
}
