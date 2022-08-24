#pragma once

#include "foundation/Math.h"
#include "ui/View.h"

namespace fw {
	class CanvasTest : public View {
	private:
		f32 _phase = 0.0f;
		f32 _scale = 1.0f;

	public:
		CanvasTest() : View({ 1024, 768 }) { setType<CanvasTest>(); }
		~CanvasTest() = default;

		void onUpdate(f32 delta) override {
			_phase = fmod(_phase + delta, PI2);
		}

		void onRender(Canvas& canvas) override {
			canvas
				.setScale({ 2.0f, 2.0f })
				.setRotation(_phase)
				.setTranslation({ 200, 200 })
				.fillRect(Rect{ -50, -50, 100, 100 }, Color4F(1, 0, 0, 1));
		}
	};
}
