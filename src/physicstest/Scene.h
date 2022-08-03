#pragma once

#include <entt/entity/registry.hpp>

#include "platform/Types.h"
#include "RpMath.h"

#include "graphics/Canvas.h"
#include "graphics/BgfxTexture.h"

#include "application/Window.h"

namespace rp {
	class Scene : public rp::app::Window {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		entt::resource<engine::Texture> _upTex;

		entt::entity _ground;
		entt::entity _ball;

		bool _physicsDebug = true;

		PointF _lastMousePos;

	public:
		Scene() : Window("Physics Test", { 1366, 768 }) {}
		~Scene() = default;

		void onInitialize() override;

		void onFrame(f32 delta) override;

		void render(Dimension res);

		void onMouseButton(MouseButton::Enum button, bool down) override;

		void onMouseMove(rp::PointF position) override;
	};
}
