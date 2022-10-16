#pragma once

#include <entt/entity/registry.hpp>

#include "graphics/Canvas.h"

#include "engine/CameraComponents.h"
#include "engine/RendererInfoSingleton.h"
#include "engine/SceneGraphUtil.h"
#include "engine/SpriteComponent.h"
#include "engine/TransformComponent.h"
#include "engine/WorldUtil.h"

namespace fw {
	struct UpdateCameraSystem {
		static void update(entt::registry& reg) {
			for (auto&& [entity, orth, trans] : reg.view<OrthographicCameraComponent, TransformComponent>().each()) {
				CameraSingleton& cam = reg.ctx().at<CameraSingleton>();
				cam.projection.dimensions = cam.viewPort.dimensions;
				cam.projection.x = -cam.viewPort.w / 2;
				cam.projection.y = -cam.viewPort.h / 2;
				cam.projection.position += trans.position;

				return;
			}
		}
	};

	struct SpriteRenderSystem {
		static void update(entt::registry& reg, Canvas& canvas) {
			SceneGraphUtil::eachRecursive(reg, WorldUtil::getRootEntity(reg), [&](entt::entity e) {
				const SpriteRenderComponent* sprite = reg.try_get<SpriteRenderComponent>(e);

				if (sprite) {
					WorldTransformComponent trans = reg.get<WorldTransformComponent>(e);
					//trans.transform.setTranslation(trans.transform.getTranslation());

					canvas.setTransform(trans.transform);
					canvas.texture(sprite->tile.handle, sprite->renderArea, sprite->tile.uvArea, sprite->color);
				}
			});
		}
	};

	struct GridComponent {
		f32 spacing = 100.0f;
	};

	struct GridRenderSystem {
		static void update(entt::registry& reg, Canvas& canvas) {
			reg.view<GridComponent>().each([&](entt::entity e, const GridComponent& grid) {
				CameraSingleton& camera = reg.ctx().at<CameraSingleton>();
				entt::entity cameraEntity = WorldUtil::getUniqueEntity<ActiveCameraTag>(reg);

				PointF cameraPos = reg.get<TransformComponent>(cameraEntity).position;

				const Color4F gridLineColor(0.3f, 0.3f, 0.3f, 1.0f);
				RectF area = camera.projection;
				area.position.x -= camera.viewPort.x;
				area.position.y -= camera.viewPort.y;
				//area.x -= area.w / 2;
				//area.y -= area.h / 2;
				//area.position += cameraPos;

				canvas.fillRect(area, Color4F::darkGrey);

				area.x -= fmod(cameraPos.x, grid.spacing) + grid.spacing;
				area.y -= fmod(cameraPos.y, grid.spacing) + grid.spacing;

				f32 pxoff = 2.0f / area.w;

				for (f32 x = area.x; x < area.w; x += grid.spacing) {
					canvas.line(x + pxoff, area.y, x + pxoff, area.h, gridLineColor);
				}

				for (f32 y = area.y; y < area.h; y += grid.spacing) {
					canvas.line(area.x, y, area.w, y, gridLineColor);
				}
			});
		}
	};
}
