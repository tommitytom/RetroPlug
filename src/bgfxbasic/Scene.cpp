#include "Scene.h"

using namespace rp;

void Scene::init() {
	/*b2Vec2 gravity(0.0f, -10.0f);
	PhysicsWorldSingleton& physicsWorld = _registry.set<PhysicsWorldSingleton>(PhysicsWorldSingleton{ .world = b2World(gravity) });

	entt::entity e = _registry.create();

	b2BodyDef groundBodyDef;
	groundBodyDef.position.Set(0.0f, -10.0f);

	b2PolygonShape groundBox;
	groundBox.SetAsBox(50.0f, 10.0f);

	Box2dUtil::addRigidBody(_registry, e, groundBodyDef, groundBox, 0.0f);

	_viewPort = { -20, -20, 40, 40 };

	b2Vec2 topl(-20, -20);
	b2Vec2 mid(0, 0);
	b2Vec2 pos(-10, 10); // -0.5, 0.5

	f32 wc = 2.0f / _viewPort.w; // 0.05*/
}

void Scene::render() {
	_canvas.beginRender({ 0, 0 }, 1.0f);

	_canvas.fillRect({ -0.5f, 0.5f, 1.0f, 1.0f }, { 1, 1, 1, 1 });

	_canvas.endRender();
	bgfx::frame();
}
