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
