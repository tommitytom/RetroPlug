#include "LayoutUtil.h"

#include <entt/entity/registry.hpp>
#include <yoga/Yoga.h>

#include "ui/next/StyleComponents.h"

namespace fw {
	void emptyValueSetter(YGNodeRef node, f32 value) {}
	void emptyAutoSetter(YGNodeRef node) {}
	void emptyEdgeValueSetter(YGNodeRef node, YGEdge edge, f32 value) {}
	void emptyEdgeAutoSetter(YGNodeRef node, YGEdge edge) {}

	template <typename T, typename TargetT, auto SetValueFunc>
	void updateYogaEnum(entt::registry& reg) {
		static_assert(std::is_enum_v<decltype(T::value)>);
		
		reg.view<T, LayoutDirtyTag>().each([&](const StyleHandle styleHandle, const T& style) {
			const DomElementHandle elementHandle = reg.get<ElementReferenceComponent>(styleHandle).handle;
			YGNodeRef node = reg.get<YGNodeRef>(elementHandle);

			SetValueFunc(node, (TargetT)style.value);
		});
	}

	template <typename T, auto SetPointFunc, auto SetPercentFunc = emptyValueSetter, auto SetAutoFunc = emptyAutoSetter>
	void updateYogaStyle(entt::registry& reg) {
		using ValueType = decltype(T::value);
		
		reg.view<T, LayoutDirtyTag>().each([&](const StyleHandle styleHandle, const T& style) {
			const DomElementHandle elementHandle = reg.get<ElementReferenceComponent>(styleHandle).handle;
			YGNodeRef node = reg.get<YGNodeRef>(elementHandle);

			if constexpr (std::is_same_v<ValueType, FlexValue>) {
				const FlexValue& value = style.value;

				switch (value.getUnit()) {
				case FlexUnit::Point: SetPointFunc(node, value.getValue()); break;
				case FlexUnit::Percent: SetPercentFunc(node, value.getValue()); break;
				case FlexUnit::Auto: SetAutoFunc(node); break;
				default: assert(false);
				}
			}

			if constexpr (std::is_same_v<ValueType, f32>) {
				SetPointFunc(node, style.value);
			}
		});
	}

	

	template <typename T, FlexEdge Edge, auto SetPointFunc, auto SetPercentFunc = emptyEdgeValueSetter, auto SetAutoFunc = emptyEdgeAutoSetter>
	void updateYogaEdgeStyle(entt::registry& reg) {
		using ValueType = decltype(T::value);

		reg.view<T, LayoutDirtyTag>().each([&](const StyleHandle styleHandle, const T& style) {
			const DomElementHandle elementHandle = reg.get<ElementReferenceComponent>(styleHandle).handle;
			YGNodeRef node = reg.get<YGNodeRef>(elementHandle);

			if constexpr (std::is_same_v<ValueType, FlexValue>) {
				const FlexValue& value = style.value;

				switch (value.getUnit()) {
				case FlexUnit::Point: SetPointFunc(node, (YGEdge)Edge, value.getValue()); break;
				case FlexUnit::Percent: SetPercentFunc(node, (YGEdge)Edge, value.getValue()); break;
				case FlexUnit::Auto: SetAutoFunc(node, (YGEdge)Edge); break;
				default: assert(false);
				}
			}

			if constexpr (std::is_same_v<ValueType, LengthValue>) {
				const LengthValue& value = style.value;

				switch (value.type) {
				case LengthType::Point: SetPointFunc(node, (YGEdge)Edge, value.value); break;
				case LengthType::Pixel: SetPointFunc(node, (YGEdge)Edge, value.value); break;
				//default: assert(false);
				}
			}

			if constexpr (std::is_same_v<ValueType, f32>) {
				SetPointFunc(node, (YGEdge)Edge, style.value);
			}
		});
	}
	
	void LayoutUtil::update(entt::registry& reg, f32 dt) {
		if (!reg.view<LayoutDirtyTag>().empty()) {
			YGNodeRef defStyle = reg.ctx().at<YGNodeRef>();
			reg.view<LayoutDirtyTag>().each([&](StyleHandle style) {
				YGNodeRef node = reg.get<YGNodeRef>(reg.get<ElementReferenceComponent>(style).handle);
				YGNodeCopyStyle(node, defStyle);
			});
			
			updateYogaEdgeStyle<styles::MarginBottom, FlexEdge::Bottom, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto>(reg);
			updateYogaEdgeStyle<styles::MarginTop, FlexEdge::Top, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto>(reg);
			updateYogaEdgeStyle<styles::MarginLeft, FlexEdge::Left, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto>(reg);
			updateYogaEdgeStyle<styles::MarginRight, FlexEdge::Right, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto>(reg);

			updateYogaEdgeStyle<styles::PaddingBottom, FlexEdge::Bottom, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent>(reg);
			updateYogaEdgeStyle<styles::PaddingTop, FlexEdge::Top, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent>(reg);
			updateYogaEdgeStyle<styles::PaddingLeft, FlexEdge::Left, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent>(reg);
			updateYogaEdgeStyle<styles::PaddingRight, FlexEdge::Right, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent>(reg);

			updateYogaEdgeStyle<styles::BorderBottomWidth, FlexEdge::Bottom, YGNodeStyleSetBorder>(reg);
			updateYogaEdgeStyle<styles::BorderTopWidth, FlexEdge::Top, YGNodeStyleSetBorder>(reg);
			updateYogaEdgeStyle<styles::BorderLeftWidth, FlexEdge::Left, YGNodeStyleSetBorder>(reg);
			updateYogaEdgeStyle<styles::BorderRightWidth, FlexEdge::Right, YGNodeStyleSetBorder>(reg);

			updateYogaEdgeStyle<styles::Bottom, FlexEdge::Bottom, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent>(reg);
			updateYogaEdgeStyle<styles::Top, FlexEdge::Top, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent>(reg);
			updateYogaEdgeStyle<styles::Left, FlexEdge::Left, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent>(reg);
			updateYogaEdgeStyle<styles::Right, FlexEdge::Right, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent>(reg);

			updateYogaEnum<styles::Position, YGPositionType, YGNodeStyleSetPositionType>(reg);
			updateYogaEnum<styles::Overflow, YGOverflow, YGNodeStyleSetOverflow>(reg);
			updateYogaEnum<styles::AlignItems, YGAlign, YGNodeStyleSetAlignItems>(reg);
			updateYogaEnum<styles::AlignContent, YGAlign, YGNodeStyleSetAlignContent>(reg);
			updateYogaEnum<styles::AlignSelf, YGAlign, YGNodeStyleSetAlignSelf>(reg);
			updateYogaEnum<styles::JustifyContent, YGJustify, YGNodeStyleSetJustifyContent>(reg);
			updateYogaEnum<styles::FlexDirection, YGFlexDirection, YGNodeStyleSetFlexDirection>(reg);
			updateYogaEnum<styles::FlexWrap, YGWrap, YGNodeStyleSetFlexWrap>(reg);

			updateYogaStyle<styles::FlexBasis, YGNodeStyleSetFlexBasis, YGNodeStyleSetFlexBasisPercent, YGNodeStyleSetFlexBasisAuto>(reg);
			updateYogaStyle<styles::FlexGrow, YGNodeStyleSetFlexGrow>(reg);
			updateYogaStyle<styles::FlexShrink, YGNodeStyleSetFlexShrink>(reg);

			updateYogaStyle<styles::Width, YGNodeStyleSetWidth, YGNodeStyleSetWidthPercent, YGNodeStyleSetWidthAuto>(reg);
			updateYogaStyle<styles::Height, YGNodeStyleSetHeight, YGNodeStyleSetHeightPercent, YGNodeStyleSetHeightAuto>(reg);
			updateYogaStyle<styles::MinWidth, YGNodeStyleSetMinWidth, YGNodeStyleSetMinWidthPercent>(reg);
			updateYogaStyle<styles::MinHeight, YGNodeStyleSetMinHeight, YGNodeStyleSetMinHeightPercent>(reg);
			updateYogaStyle<styles::MaxWidth, YGNodeStyleSetMaxWidth, YGNodeStyleSetMaxWidthPercent>(reg);
			updateYogaStyle<styles::MaxHeight, YGNodeStyleSetMaxHeight, YGNodeStyleSetMaxHeightPercent>(reg);

			reg.clear<LayoutDirtyTag>();
		}
	}
}
