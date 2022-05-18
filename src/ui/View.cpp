#include "View.h"

#include <nanovg.h>

using namespace rp;

void View::drawRect(const Rect<f32>& area, const NVGcolor& color) {
	NVGcontext* vg = getVg();
	nvgBeginPath(vg);
	nvgRect(vg, area.x, area.y, area.w, area.h);
	nvgFillColor(vg, color);
	nvgFill(vg);
}

void View::drawText(f32 x, f32 y, std::string_view text, const NVGcolor& color) {
	NVGcontext* vg = getVg();
	nvgFontSize(vg, 14.0f);
	nvgFontFace(vg, "Roboto-Regular");
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFillColor(vg, color);
	nvgStrokeColor(vg, color);
	nvgText(vg, x, y, text.data(), NULL);
}
