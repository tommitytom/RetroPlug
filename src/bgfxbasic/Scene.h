#pragma once

#include <entt/entity/registry.hpp>

#include "platform/Types.h"
#include "RpMath.h"

#include "BgfxCanvas.h"

namespace rp {
	struct SpriteComponent {

	};

	class Scene {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		rp::engine::BgfxCanvas _canvas;

		rp::engine::CanvasTextureHandle _upTex;

		entt::entity _ground;
		entt::entity _ball;

	public:
		Scene() = default;
		~Scene() = default;

		void init();

		void update(f32 delta);

		void render(Dimension res);
	};
}
