#include "Scene.h"

#include "PhysicsComponents.h"
#include "Box2dUtil.h"

using namespace rp;

void Scene::init() {
	b2Vec2 gravity(0.0f, 10.0f);

	PhysicsWorldSingleton& physicsWorld = _registry.set<PhysicsWorldSingleton>(PhysicsWorldSingleton{ 
		.world = std::make_unique<b2World>(gravity)
	});

	_ground = _registry.create();
	_ball = _registry.create();

	Box2dUtil::addBox(_registry, _ground, RectF(50, 768 - 150, 1024 - 100, 50));
	Box2dUtil::addCircle(_registry, _ball, PointF(300, 300), 50, 10.0f);

	f32 xoff = 0.0f;
	for (size_t i = 0; i < 30; ++i) {
		entt::entity e = _registry.create();
		Box2dUtil::addBox(_registry, e, RectF(200 + xoff, i * 30.0f, 20, 20), 10.0f);
		xoff += 2.0f;
	}

	//Box2dUtil::addBox(_registry, _ground, RectF(110, 110, 100, 100));
	//_canvas.fillRect({ 110, 110, 100, 100 }, { 0, 1, 0, 1 });

	_upTex = _canvas.loadTexture("up.png");
}

void Scene::update(f32 delta) {
	PhysicsWorldSingleton& physicsWorld = _registry.ctx<PhysicsWorldSingleton>();
	physicsWorld.world->Step(1.0f / 60.0f, 6, 2);
}

void Scene::render(Dimension res) {
	_canvas.beginRender(res, 1.0f);

	PhysicsWorldSingleton& physicsWorld = _registry.ctx<PhysicsWorldSingleton>();

	for (auto [e, comp] : _registry.view<PhysicsComponent>().each()) {
		const b2Shape* shape = comp.fixture->GetShape();
		
		switch (shape->GetType()) {
			case b2Shape::Type::e_polygon: {
				const b2PolygonShape* polygon = static_cast<const b2PolygonShape*>(shape);

				PointF points[8];

				for (int32 i = 0; i < polygon->m_count; ++i) {
					points[i] = 
						Box2dUtil::convert(comp.body->GetWorldPoint(polygon->m_vertices[i])) * 
						physicsWorld.meterSize;
				}
				
				if (polygon->m_count == 4) {
					_canvas.polygon(points, polygon->m_count);
				}

				break;
			}
			case b2Shape::Type::e_circle: {
				const b2CircleShape* circle = static_cast<const b2CircleShape*>(shape);
				PointF point = Box2dUtil::convert(comp.body->GetWorldPoint(circle->m_p));
				_canvas.circle(point * physicsWorld.meterSize, circle->m_radius * physicsWorld.meterSize);

				break;
			}
		}
	}

	//RectF r = { 10, 10, 100, 100 };
	//PointF points[4] = { r.position, r.topRight(), r.bottomRight(), r.bottomLeft() };

	//_canvas.polygon(points, 4);

	//_canvas.fillRect({ -0.5f, 0.5f, 1.0f, 1.0f }, { 1, 1, 1, 1 });
	_canvas.fillRect({ 10, 10, 100, 100 }, { 1, 0, 0, 1 });

	_canvas.circle({ 300, 50 }, 20, 32);

	//_canvas.fillRect({ 100, 100, 100, 100 }, { 0, 1, 0, 1 });
	//_canvas.texture(_upTex, { -0.5f, 0.5f, 1.0f, 1.0f }, { 1, 1, 1, 1 });
	
	_canvas.endRender();
}
