#pragma once

#include "ui/TextureView.h"
#include "lsdj/LsdjCanvas.h"

namespace rp {
	class LsdjCanvasView : public TextureView {
	protected:
		lsdj::Canvas _canvas;

	public:
		LsdjCanvasView(Dimension<uint32> dimensions = { 100, 100 }) : TextureView(dimensions), _canvas(dimensions) {
			setType<LsdjCanvasView>();
		}

		LsdjCanvasView(Dimension<uint32> dimensions, const lsdj::Font& font, const lsdj::Palette& palette): TextureView(dimensions), _canvas(dimensions, font, palette) {
			setType<LsdjCanvasView>(); 
		}

		~LsdjCanvasView() {}

		virtual void onRender() override {
			setImage(_canvas.getRenderTarget());			
			TextureView::onRender();
		}
	};
}
