#pragma once

#include "ui/View.h"
#include "application/Application.h"

namespace fw {
	class ${name} : public View {
	private:

	public:
		${name}() : View({ 1024, 768 }) {
			setType<${name}>();
			setSizingPolicy(SizingPolicy::FitToParent);
			setFocusPolicy(FocusPolicy::Click);
		}

		~${name}() = default;

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

	using ${name}Application = fw::app::BasicApplication<${name}, void>;
}
