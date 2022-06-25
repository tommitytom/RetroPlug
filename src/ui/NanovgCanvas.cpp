#include "NanovgCanvas.h"

#include <nanovg.h>

#include "fonts/Roboto-Regular.h"

using namespace rp;

NanovgCanvas::NanovgCanvas() {

}

NanovgCanvas::~NanovgCanvas() {
	if (_vg) {
		nvgDelete(_vg);
	}
}

void NanovgCanvas::beginRender(Dimension res, f32 pixelRatio) {
	nvgBeginFrame(_vg, (f32)res.w, (f32)res.h, pixelRatio);
}

void NanovgCanvas::endRender() {
	nvgEndFrame(_vg);
}

void NanovgCanvas::translate(PointF amount) {
	nvgTranslate(_vg, amount.x, amount.y);
}

void NanovgCanvas::setScale(f32 scaleX, f32 scaleY) {
	nvgScale(_vg, scaleX, scaleY);
}

void NanovgCanvas::init() {
	_vg = nvgCreate(0, 0);
	bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

	nvgCreateFontMem(_vg, "Roboto-Regular", Roboto_Regular, (int)Roboto_Regular_len, 0);
}

void NanovgCanvas::fillRect(const RectT<f32>& area, const Color4F& color) {
	if (area.w > 0 && area.h > 0) {
		nvgBeginPath(_vg);
		nvgRect(_vg, area.x, area.y, area.w, area.h);
		nvgFillColor(_vg, *((NVGcolor*)&color));
		nvgFill(_vg);
	}
}

void NanovgCanvas::strokeRect(const RectT<f32>& area, const Color4F& color) {
	if (area.w > 0 && area.h > 0) {
		nvgBeginPath(_vg);
		nvgRect(_vg, area.x, area.y, area.w, area.h);
		nvgFillColor(_vg, *((NVGcolor*)&color));
		nvgFill(_vg);
	}
}

void NanovgCanvas::text(f32 x, f32 y, std::string_view text, const Color4F& color) {
	nvgFontSize(_vg, 14.0f);
	nvgFontFace(_vg, "Roboto-Regular");
	nvgTextAlign(_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFillColor(_vg, *((NVGcolor*)&color));
	nvgStrokeColor(_vg, *((NVGcolor*)&color));
	nvgText(_vg, x, y, text.data(), NULL);
}
