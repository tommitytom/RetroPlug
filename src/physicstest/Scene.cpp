#include "Scene.h"

#include "PhysicsComponents.h"
#include "Box2dUtil.h"

using namespace rp;

void Scene::onInitialize() {
	b2Vec2 gravity(0.0f, 10.0f);

	PhysicsWorldSingleton& physicsWorld = _registry.ctx().emplace<PhysicsWorldSingleton>(PhysicsWorldSingleton{
		.world = std::make_unique<b2World>(gravity)
	});

	_ground = _registry.create();
	_ball = _registry.create();
	entt::entity ball2 = _registry.create();

	Box2dUtil::addBox(_registry, _ground, RectF(50, 768 - 150, 1024 - 100, 50));
	Box2dUtil::addCircle(_registry, _ball, PointF(300, 0), 50, 10.0f);
	Box2dUtil::addCircle(_registry, ball2, PointF(310, 50), 50, 10.0f);

	f32 xoff = 0.0f;
	for (size_t j = 0; j < 4; ++j) {
		for (size_t i = 0; i < 20; ++i) {
			entt::entity e = _registry.create();
			Box2dUtil::addBox(_registry, e, RectF(200 + xoff, i * 30.0f, 20, 20), 10.0f);
			xoff += 2.0f;
		}

		xoff += 50;
	}
	

	//Box2dUtil::addBox(_registry, _ground, RectF(110, 110, 100, 100));
	//getCanvas().fillRect({ 110, 110, 100, 100 }, { 0, 1, 0, 1 });

	_upTex = getCanvas().loadTexture("taco.png");
}

const f32 PHYSICS_DELTA_SECS = 1.0f / 60.0f;

void Scene::onFrame(f32 delta) {
	PhysicsWorldSingleton& physicsWorld = _registry.ctx().at<PhysicsWorldSingleton>();
	physicsWorld.delta += delta;

	if (physicsWorld.delta > PHYSICS_DELTA_SECS) {
		physicsWorld.delta = fmod(physicsWorld.delta, PHYSICS_DELTA_SECS);
		physicsWorld.world->Step(PHYSICS_DELTA_SECS, 6, 2);
	}

	render(getDimensions());
}

void Scene::render(Dimension res) {
	PhysicsWorldSingleton& physicsWorld = _registry.ctx().at<PhysicsWorldSingleton>();

	if (_physicsDebug) {
		f32 scale = physicsWorld.meterSize;

		for (auto [e, comp] : _registry.view<PhysicsComponent>().each()) {
			const b2Shape* shape = comp.fixture->GetShape();

			switch (shape->GetType()) {
			case b2Shape::Type::e_polygon:
			{
				const b2PolygonShape* polygon = static_cast<const b2PolygonShape*>(shape);

				PointF points[8];

				for (int32 i = 0; i < polygon->m_count; ++i) {
					points[i] =
						Box2dUtil::convert(comp.body->GetWorldPoint(polygon->m_vertices[i])) *
						scale;
				}

				if (polygon->m_count == 4) {
					getCanvas().polygon(points, polygon->m_count);
				}

				break;
			}
			case b2Shape::Type::e_circle:
			{
				const b2CircleShape* circle = static_cast<const b2CircleShape*>(shape);
				PointF point = Box2dUtil::convert(comp.body->GetWorldPoint(circle->m_p)) * scale;
				f32 radius = circle->m_radius * scale;
				f32 angle = comp.body->GetAngle();
				PointF angleLine = PointF(cos(angle), sin(angle)) * radius;

				getCanvas()
					.circle(point, radius, 32, Color4F(1, 0, 0, 1))
					.line(point, point + angleLine, Color4F(1, 1, 1, 1));

				break;
			}
			}
		}
	}

	RectF r = { 10, 10, 100, 100 };
	PointF points[4] = { r.position, r.topRight(), r.bottomRight(), r.bottomLeft() };

	//getCanvas().points(points, 4);

	//getCanvas().polygon(points, 4);

	//getCanvas().fillRect({ -0.5f, 0.5f, 1.0f, 1.0f }, { 1, 1, 1, 1 });
	//getCanvas().fillRect({ 10, 10, 100, 100 }, { 0, 1, 0, 1.f });
	//getCanvas().fillRect({ 60, 60, 100, 100 }, { 1, 0, 0, 1.f });

	//getCanvas().line({ 300, 300 }, { 400, 400 }, { 1, 1, 1, 1 });

	//getCanvas().circle({ 300, 50 }, 20, 32);

	//getCanvas().fillRect({ 100, 100, 100, 100 }, { 0, 1, 0, 1 });
	//getCanvas().texture(_upTex, { 100, 100, 100, 100 }, { 1, 1, 1, 1 });

	//getCanvas().texture(engine::CanvasTextureHandle(1), { 100, 100, 512, 512 }, Color4F(1, 1, 1, 1));

	//getCanvas().text(100, 100, "Hello world!", Color4F(1, 1, 1, 1));
}

void Scene::onMouseMove(rp::PointF position) {
	_lastMousePos = position;
}

void Scene::onMouseButton(MouseButton::Enum button, bool down) {
	entt::entity e = _registry.create();

	if (button == MouseButton::Left) {
		Box2dUtil::addBox(_registry, e, RectF(_lastMousePos.x, _lastMousePos.y, 20, 20), 10.0f);
	} else {
		Box2dUtil::addCircle(_registry, e, _lastMousePos, 50, 10.0f);
	}
}
