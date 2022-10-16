#pragma once

#include <functional>
#include "foundation/Types.h"

namespace fw::Curves {
	using Func = std::function<f32(f32)>;

	static f32 linear(f32 v) { return v; }

	static f32 pow1(f32 v) { return v * v; }

	static f32 pow2(f32 v) { return v * v * v; }

	static f32 pow3(f32 v) { return v * v * v * v; }
}