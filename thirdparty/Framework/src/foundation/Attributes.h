#pragma once

#include <refl.hpp>
#include "foundation/Types.h"

namespace fw {
	struct RangeAttribute : refl::attr::usage::field {
		const f32 min;
		const f32 max;

		constexpr RangeAttribute(const RangeAttribute& attrib) : min(attrib.min), max(attrib.max) {}
		constexpr RangeAttribute(f32 _min, f32 _max) : min(_min), max(_max) {}
	};

	struct StepSizeAttribute : refl::attr::usage::field {
		const f32 value;

		constexpr StepSizeAttribute(const StepSizeAttribute& attrib) : value(attrib.value) {}
		constexpr StepSizeAttribute(f32 _value) : value(_value) {}
	};
}
