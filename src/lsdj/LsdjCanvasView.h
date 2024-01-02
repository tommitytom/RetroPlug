#pragma once

#include "ui/TextureView.h"
#include "lsdj/LsdjCanvas.h"

namespace rp {
	class LsdjCanvasView : public fw::TextureView {
		RegisterObject();
	protected:
		lsdj::Canvas _canvas;

	public:
		LsdjCanvasView(fw::Dimension dimensions = { 100, 100 }) : fw::TextureView(), _canvas((fw::DimensionU32)dimensions) {
		}

		LsdjCanvasView(fw::Dimension dimensions, const lsdj::Font& font, const lsdj::Palette& palette) : fw::TextureView(), _canvas((fw::DimensionU32)dimensions, font, palette) {
		}

		~LsdjCanvasView() {}

		virtual void onRender(fw::Canvas& canvas) override {
			setImage(_canvas.getRenderTarget());
			fw::TextureView::onRender(canvas);
		}
	};
}
