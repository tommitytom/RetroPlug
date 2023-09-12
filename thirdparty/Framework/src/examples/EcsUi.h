#pragma once

#include "ui/View.h"

#include "application/Application.h"

namespace fw {
	class LuaReact;

	class EcsUi : public View {
		RegisterObject()
			
	private:
		std::shared_ptr<LuaReact> _react;

	public:
		EcsUi();
		~EcsUi();
		
		void onInitialize() override;

		bool onKey(const KeyEvent& ev) override;

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;

		void onHotReload() override;

		bool onMouseMove(Point pos) override;

		bool onMouseButton(const MouseButtonEvent& ev) override;

	private:
		bool propagateMouseMove(entt::entity e, PointF pos);
	};

	using EcsUiApplication = fw::app::BasicApplication<EcsUi, void>;
}
