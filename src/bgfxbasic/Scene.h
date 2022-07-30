#pragma once

#include <entt/entity/registry.hpp>

#include "platform/Types.h"
#include "RpMath.h"

#include "graphics/BgfxCanvas.h"

namespace rp {
	class Scene {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		rp::engine::BgfxCanvas _canvas;

		rp::engine::CanvasTextureHandle _upTex;

		entt::entity _ground;
		entt::entity _ball;

		bool _physicsDebug = true;

		PointF _lastMousePos;

	public:
		Scene() = default;
		~Scene() = default;

		void init();

		void update(f32 delta);

		void render(Dimension res);

		void onMouseMove(f32 x, f32 y);

		void onMouseButton(int button, int action, int mods);
	};
}
