#pragma once

#include <vector>
#include <refl.hpp>
#include "foundation/Types.h"

namespace fw::reflutil {
	struct RangeAttribute : refl::attr::usage::field {
		const f32 min;
		const f32 max;

		constexpr RangeAttribute(const RangeAttribute& attrib) : min(attrib.min), max(attrib.max) {}
		constexpr RangeAttribute(f32 _min, f32 _max) : min(_min), max(_max) {}
	};

	template <typename T>
	struct OptionsAttribute : refl::attr::usage::field {
		const std::vector<T> values;

		constexpr OptionsAttribute(const OptionsAttribute& attrib) : values(attrib.values) {}
		constexpr OptionsAttribute(const std::vector<T>& items) : values(items) {}
	};

	template <typename T>
	struct DefaultAttribute : refl::attr::usage::field {
		const T value;

		constexpr DefaultAttribute(const DefaultAttribute& attrib) : value(attrib.value) {}
		constexpr DefaultAttribute(const T& _value) : value(_value) {}
	};

	struct StepSizeAttribute : refl::attr::usage::field {
		const f32 value;

		constexpr StepSizeAttribute(const StepSizeAttribute& attrib) : value(attrib.value) {}
		constexpr StepSizeAttribute(f32 _value) : value(_value) {}
	};

	template <typename T>
	struct TypedAttribute : refl::attr::usage::field {
		const f32 min;
		const f32 max;

		constexpr TypedAttribute(const TypedAttribute& attrib) : min(attrib.min), max(attrib.max) {}
		constexpr TypedAttribute(f32 _min, f32 _max) : min(_min), max(_max) {}
	};
}
