#pragma once

#include "ui/View.h"
#include "application/Application.h"

namespace fw {
	class RetroPlug : public View {
	private:

	public:
		RetroPlug() : View({ 1024, 768 }) {
			setType<RetroPlug>();
			setSizingPolicy(SizingPolicy::FitToParent);
			setFocusPolicy(FocusPolicy::Click);
		}

		~RetroPlug() = default;

		void onInitialize() override {

		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			return false;
		}

		bool onKey(const KeyEvent& ev) override {
			return false;
		}

		void onUpdate(f32 delta) override {

		}

		void onRender(orb::Canvas& canvas) override {

		}
	};

	using RetroPlugApplication = orb::app::BasicApplication<RetroPlug, void>;
}
