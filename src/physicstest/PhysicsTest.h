#pragma once

#include <entt/entity/registry.hpp>

#include "platform/Types.h"
#include "RpMath.h"

#include "graphics/Canvas.h"
#include "graphics/BgfxTexture.h"

#include "application/Window.h"

namespace rp {
	class PhysicsTest : public View {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		TypedResourceHandle<engine::Texture> _upTex;

		entt::entity _ground;
		entt::entity _ball;

		bool _physicsDebug = true;

		PointF _lastMousePos;

	public:
		PhysicsTest() : View({ 1366, 768 }) {
			setType<PhysicsTest>();
			setName("Physics Test");
		}
		~PhysicsTest() = default;

		void onInitialize() override;

		void onUpdate(f32 delta) override;

		void onRender(engine::Canvas& canvas) override;

		bool onMouseButton(MouseButton::Enum button, bool down, rp::Point position) override;

		bool onMouseMove(rp::Point position) override;
	};
}
