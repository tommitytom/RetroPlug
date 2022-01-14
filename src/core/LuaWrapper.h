#pragma once

#include <sol/sol.hpp>
#include <nanovg.h>

namespace rp {
	struct NVGcontextWrapper {
		NVGcontext* vg;
	};

	void setupLuaWrapper(sol::state& s);
}
