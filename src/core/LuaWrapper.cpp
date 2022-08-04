#include "LuaWrapper.h"

void rp::setupLuaWrapper(sol::state& s) {
	/*s.new_usertype<NVGcolor>("NVGcolor",
		"r", &NVGcolor::r,
		"g", &NVGcolor::b,
		"b", &NVGcolor::b,
		"a", &NVGcolor::a
	);

	s.new_usertype<NVGcontext>("NVGcontext",
		"beginPath", nvgBeginPath,
		"rect", nvgRect,
		"fillColor", nvgFillColor,
		"fill", nvgFill
	);

	s.new_usertype<NVGcontextWrapper>("NVGcontextWrapper",
		"beginPath", [](NVGcontextWrapper& wrapper) { nvgBeginPath(wrapper.vg); },
		"rect", [](NVGcontextWrapper& wrapper, float x, float y, float w, float h) { nvgRect(wrapper.vg, x, y, w, h); },
		"fillColor", [](NVGcontextWrapper& wrapper, float r, float g, float b, float a) { nvgFillColor(wrapper.vg, NVGcolor{ r, g, b, a }); },
		"fill", [](NVGcontextWrapper& wrapper) { nvgFill(wrapper.vg); }
	);*/
}
