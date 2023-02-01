#pragma once

#include "ui/View.h"

#include "engine/EngineSystems.h"
#include "engine/EngineUtil.h"
#include "engine/RelationshipComponent.h"
#include "engine/RendererInfoSingleton.h"
#include "engine/SceneGraphUtil.h"
#include "engine/SpriteComponent.h"
#include "engine/TransformComponent.h"

namespace fw {
	class SceneView : public View {
	private:
		entt::registry _registry;

	public:
		SceneView(Dimension dimensions = { 100, 100 }) : View(dimensions) {
			setType<SceneView>();
			setFocusPolicy(FocusPolicy::Click);
		}

		void onInitialize() override {
			ResourceManager& rm = this->getResourceManager();
			rm.setRootPath("../../resources");
			spdlog::info("Set resources path to {}", rm.getRootPath().string());

			_registry = WorldUtil::createWorld(&rm);

			_registry.ctx().emplace<RendererInfoSingleton>(RendererInfoSingleton{
				.dimensions = getDimensions()
			});

			RectF worldArea = (RectF)getWorldArea();

			_registry.ctx().emplace<CameraSingleton>(CameraSingleton{
				.viewPort = worldArea,
				.projection = worldArea
			});
		}

		void onResize(const ResizeEvent& ev) override {
			_registry.ctx().at<RendererInfoSingleton>().dimensions = ev.size;

			CameraSingleton& camera = _registry.ctx().at<CameraSingleton>();
			camera.viewPort = (RectF)getWorldArea();
		}

		void onUpdate(f32 delta) override {
			SceneGraphUtil::updateWorldTransforms(_registry, WorldUtil::getRootEntity(_registry));
			UpdateCameraSystem::update(_registry);
			WorldUtil::clearEvents(_registry);
		}

		void onRender(fw::Canvas& canvas) override {
			beginSceneRender(_registry, canvas);
			SpriteRenderSystem::update(_registry, canvas);
			endSceneRender(canvas);
		}

		entt::registry& getRegistry() {
			return _registry;
		}

		const entt::registry& getRegistry() const {
			return _registry;
		}

	protected:
		void beginSceneRender(const entt::registry& reg, Canvas& canvas) {
			const CameraSingleton& camera = reg.ctx().at<CameraSingleton>();
			canvas.setViewProjection((Rect)camera.viewPort, camera.projection);
		}

		void endSceneRender(fw::Canvas& canvas) {
			canvas.resetViewProjection();
		}
	};
}
