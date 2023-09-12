#pragma once

#include <entt/entity/registry.hpp>
#include "foundation/Math.h"
#include "ui/Flex.h"
#include "StyleProperties.h"
//#include "StyleComponents.h"

namespace fw {	
	using PropertySetFunc = std::function<void(entt::registry& reg, const StyleHandle e, const entt::any& data)>;
	using PropertyCopyFunc = std::function<bool(entt::registry& reg, const StyleHandle from, const StyleHandle to)>;
	using PropertyTransitionFunc = std::function<void(entt::registry&, const StyleHandle, const StyleHandle, const StyleHandle, const f32)>;

	struct PropertyVTable {
		PropertySetFunc set;
		PropertyCopyFunc copy;
		PropertyTransitionFunc transition;
	};
	
	template <typename T>
	T handleValueTransition(const T& from, const T& to, f32 frac);

	template <>
	inline Color4F handleValueTransition<Color4F>(const Color4F& from, const Color4F& to, f32 frac) {
		return Color4F{
			from.r + (to.r - from.r) * frac,
			from.g + (to.g - from.g) * frac,
			from.b + (to.b - from.b) * frac,
			from.a + (to.a - from.a) * frac,
		};
	}

	template <>
	inline FlexValue handleValueTransition<FlexValue>(const FlexValue& from, const FlexValue& to, f32 frac) {
		if (from.getUnit() == to.getUnit()) {
			f32 range = to.getValue() - from.getValue();
			return FlexValue(to.getUnit(), from.getValue() + range * frac);
		}

		return to;
	}

	template <>
	inline LengthValue handleValueTransition<LengthValue>(const LengthValue& from, const LengthValue& to, f32 frac) {
		if (from.type == to.type) {
			f32 range = to.value - from.value;
			return LengthValue(to.type, from.value + range * frac);
		}

		return to;
	}

	template <>
	inline FontWeightValue handleValueTransition<FontWeightValue>(const FontWeightValue& from, const FontWeightValue& to, f32 frac) {
		if (from.type == to.type) {
			f32 range = to.value - from.value;
			return FontWeightValue(to.type, from.value + range * frac);
		}

		return to;
	}

	template <typename T>
	void handleTransition(entt::registry& reg, const entt::entity from, const entt::entity to, const entt::entity target, f32 frac) {
		reg.emplace_or_replace<T>(target, T{ handleValueTransition(reg.get<T>(from).value, reg.get<T>(to).value, frac) });
		reg.emplace_or_replace<LayoutDirtyTag>(target);
	}

	template <typename T>
	void propertySetter(entt::registry& reg, const entt::entity e, const entt::any& data) {
		reg.emplace_or_replace<T>(e, entt::any_cast<const T&>(data));
	}

	template <typename T>
	bool propertyCopier(entt::registry& reg, const entt::entity from, const entt::entity to) {
		const T* val = reg.try_get<T>(from);
		if (val) {
			reg.emplace_or_replace<T>(to, *val);
			return true;
		}
		
		return false;
	}

	template <typename T>
	constexpr std::pair<std::string_view, PropertyVTable> makePropertyVTable() {
		std::pair<std::string_view, PropertyVTable> item = std::make_pair(T::PropertyName, PropertyVTable{
			.set = propertySetter<T>,
			.copy = propertyCopier<T>
		});

		if constexpr (entt::type_list_contains_v<typename T::Tags, AnimatableTag>) {
			item.second.transition = handleTransition<T>;
		}
		
		return item;
	}	
}
