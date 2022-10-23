#pragma once

#include "ui/TextureView.h"
#include "lsdj/LsdjCanvas.h"

namespace rp {
	class LsdjCanvasView : public fw::TextureView {
	protected:
		lsdj::Canvas _canvas;

	public:
		LsdjCanvasView(fw::Dimension dimensions = { 100, 100 }) : fw::TextureView(dimensions), _canvas((fw::DimensionU32)dimensions) {
			setType<LsdjCanvasView>();
		}

		LsdjCanvasView(fw::Dimension dimensions, const lsdj::Font& font, const lsdj::Palette& palette) : fw::TextureView(dimensions), _canvas((fw::DimensionU32)dimensions, font, palette) {
			setType<LsdjCanvasView>();
		}

		~LsdjCanvasView() {}

		virtual void onRender(Canvas& canvas) override {
			setImage(_canvas.getRenderTarget());
			fw::TextureView::onRender(canvas);
		}
	};
}
