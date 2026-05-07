#pragma once

#include "ui/TextureView.h"
#include "lsdj/LsdjCanvas.h"

namespace rp {
	class LsdjCanvasView : public orb::TextureView {
		FwRegisterObject();
	protected:
		lsdj::Canvas _canvas;

	public:
		LsdjCanvasView(orb::Dimension dimensions = { 100, 100 }) : orb::TextureView(), _canvas((orb::DimensionU32)dimensions) {
		}

		LsdjCanvasView(orb::Dimension dimensions, const lsdj::Font& font, const lsdj::Palette& palette) : orb::TextureView(), _canvas((orb::DimensionU32)dimensions, font, palette) {
		}

		~LsdjCanvasView() {}

		lsdj::Canvas& getCanvas() {
			return _canvas;
		}

		virtual void onRender(orb::Canvas& canvas) override {
			setImage(_canvas.getRenderTarget());
			orb::TextureView::onRender(canvas);
		}
	};

	using LsdjCanvasViewPtr = std::shared_ptr<LsdjCanvasView>;
}
