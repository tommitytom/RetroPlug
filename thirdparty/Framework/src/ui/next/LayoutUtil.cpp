#include "LayoutUtil.h"

#include <entt/entity/registry.hpp>
#include <yoga/Yoga.h>

#include "ui/next/StyleComponents.h"

namespace fw {
	template <typename T, auto SetPointFunc, auto SetPercentFunc, auto SetAutoFunc>
	void updateYogaStyle(entt::registry& reg) {
		reg.view<T, LayoutDirtyTag>().each([&](const StyleHandle styleHandle, const T& style) {
			const DomElementHandle elementHandle = reg.get<ElementReferenceComponent>(styleHandle).handle;
			YGNodeRef node = reg.get<YGNodeRef>(elementHandle);
			const FlexValue& value = style.value;

			switch (value.getUnit()) {
			case FlexUnit::Point: SetPointFunc(node, value.getValue()); break;
			case FlexUnit::Percent: SetPercentFunc(node, value.getValue()); break;
			case FlexUnit::Auto: SetAutoFunc(node); break;
			default: assert(false);
			}
		});
	}

	void updateYogaEdgeStyle() {

	}
	
	void LayoutUtil::update(entt::registry& reg, f32 dt) {
		updateYogaStyle<styles::Width, YGNodeStyleSetWidth, YGNodeStyleSetWidthPercent, YGNodeStyleSetWidthAuto>(reg);
		updateYogaStyle<styles::Height, YGNodeStyleSetHeight, YGNodeStyleSetHeightPercent, YGNodeStyleSetHeightAuto>(reg);
		reg.clear<LayoutDirtyTag>();
	}
}
