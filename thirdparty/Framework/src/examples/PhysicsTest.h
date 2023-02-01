#pragma once

#include <entt/entity/registry.hpp>

#include "foundation/Types.h"
#include "foundation/Math.h"

#include "graphics/Canvas.h"
#include "graphics/Texture.h"
#include "graphics/Font.h"

#include "application/Window.h"

namespace fw {
	class PhysicsTest : public View {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		TextureHandle _upTex;
		FontHandle _font;

		entt::entity _ground = entt::null;
		entt::entity _ball = entt::null;

		bool _physicsDebug = true;

		PointF _lastMousePos;

	public:
		PhysicsTest() : View({ 1366, 768 }) {
			setType<PhysicsTest>();
		}
		~PhysicsTest() = default;

		void onInitialize() override;

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override;

		bool onMouseMove(Point position) override;
	};
}
