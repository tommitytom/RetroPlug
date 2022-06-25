#pragma once

#include "ui/TextureView.h"
#include "lsdj/LsdjCanvas.h"

namespace rp {
	class LsdjCanvasView : public TextureView {
	protected:
		lsdj::Canvas _canvas;

	public:
		LsdjCanvasView(Dimension dimensions = { 100, 100 }) : TextureView(dimensions), _canvas((DimensionU32)dimensions) {
			setType<LsdjCanvasView>();
		}

		LsdjCanvasView(Dimension dimensions, const lsdj::Font& font, const lsdj::Palette& palette): TextureView(dimensions), _canvas((DimensionU32)dimensions, font, palette) {
			setType<LsdjCanvasView>(); 
		}

		~LsdjCanvasView() {}

		virtual void onRender(Canvas& canvas) override {
			setImage(_canvas.getRenderTarget());			
			TextureView::onRender(canvas);
		}
	};
}
