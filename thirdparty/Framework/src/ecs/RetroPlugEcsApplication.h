#pragma once

#include "ui/View.h"
#include "application/Application.h"

namespace fw {
	class RetroPlugEcs : public View {
	private:

	public:
		RetroPlugEcs() : View({ 1024, 768 }) {
			setType<RetroPlugEcs>();
			setSizingPolicy(SizingPolicy::FitToParent);
			setFocusPolicy(FocusPolicy::Click);
		}

		~RetroPlugEcs() = default;

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

		void onRender(fw::Canvas& canvas) override {

		}
	};

	using RetroPlugEcsApplication = fw::app::BasicApplication<RetroPlugEcs, void>;
}
