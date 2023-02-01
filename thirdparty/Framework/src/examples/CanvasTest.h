#pragma once

#include "foundation/Math.h"
#include "ui/View.h"
#include "application/Application.h"

namespace fw {
	class CanvasTest : public View {
	private:
		f32 _phase = 0.0f;
		f32 _scale = 1.0f;
		TextureHandle _circle;

	public:
		CanvasTest() : View({ 1024, 768 }) { setType<CanvasTest>(); }
		~CanvasTest() = default;

		void onInitialize() override {
			_circle = getResourceManager().load<Texture>("C:\\code\\RetroPlugNext\\thirdparty\\Framework\\resources\\textures\\circle-512.png");
		}

		void onUpdate(f32 delta) override {
			_phase = fmod(_phase + delta, PI2);
		}

		void onRender(fw::Canvas& canvas) override {
			canvas
				.setScale({ 2.0f, 2.0f })
				.setRotation(_phase)
				.setTranslation({ 200, 200 })
				.fillRect(Rect{ -50, -50, 100, 100 }, Color4F(1, 0, 0, 1))
				.setRotation(0)
				.setTranslation({ 400, 400 })
				.texture(_circle, RectF{ -50.0f, -50.0f, 100.0f, 100.0f }, Color4F(1, 1, 1, 1))
				.setTranslation({ 600, 400 })
				.text(0, 0, "Hello world!");
		}
	};

	using CanvasTestApplication = fw::app::BasicApplication<CanvasTest, void>;
}
