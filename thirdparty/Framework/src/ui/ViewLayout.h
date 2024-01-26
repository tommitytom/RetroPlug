#pragma once

#include <refl.hpp>
#include <yoga/Yoga.h>
#include <yoga/YGNode.h>

#include "foundation/Math.h"
#include "ui/Flex.h"

namespace fw {	
	class ViewLayout {
	private:
		YGNodeRef _yogaNode = nullptr;
		bool _dirty = false;
		
	public:
		ViewLayout(ViewLayout&& other) noexcept {
			*this = std::move(other);
		}

		ViewLayout() : _yogaNode(YGNodeNew()) {

		}

		ViewLayout(Dimension dimensions) : _yogaNode(YGNodeNew()) {
			setDimensions(dimensions);
			YGNodeCalculateLayout(_yogaNode, YGUndefined, YGUndefined, YGDirectionInherit);

		}

		~ViewLayout() {
			YGNodeFree(_yogaNode);
		}

		uint32 getChildCount() const {
			return YGNodeGetChildCount(_yogaNode);
		}

		YGNodeRef getChild(size_t idx) {
			return YGNodeGetChild(_yogaNode, (uint32)idx);
		}

		YGNodeConstRef getChild(size_t idx) const {
			return YGNodeGetChild(_yogaNode, (uint32)idx);
		}

		entt::entity getEntity() {
			return static_cast<entt::entity>(reinterpret_cast<std::uintptr_t>(YGNodeGetContext(_yogaNode)));
		}
		
		ViewLayout& operator=(ViewLayout&& other) noexcept {
			if (_yogaNode) {
				YGNodeFree(_yogaNode);
			}
			
			_yogaNode = other._yogaNode;
			_dirty = true;
			
			other._yogaNode = YGNodeNew();
			other._dirty = false;

			return *this;
		}

		ViewLayout& operator=(const ViewLayout& other) {
			if (_yogaNode) {
				YGNodeFree(_yogaNode);
			}
			
			_yogaNode = YGNodeClone(other._yogaNode);
			_dirty = true;

			return *this;
		}

		void setContext(void* context) {
			YGNodeSetContext(_yogaNode, context);
		}

		void setMeasureFunc(YGMeasureFunc func) {
			YGNodeSetMeasureFunc(_yogaNode, func);
		}

		void setOverflow(FlexOverflow overflow) {
			YGNodeStyleSetOverflow(_yogaNode, (YGOverflow)overflow);
			_dirty = true;
		}
		
		FlexOverflow getOverflow() const {
			return (FlexOverflow)YGNodeStyleGetOverflow(_yogaNode);
		}

		void setJustifyContent(FlexJustify justify) {
			YGNodeStyleSetJustifyContent(_yogaNode, (YGJustify)justify);			
			_dirty = true;
		}

		FlexJustify getJustifyContent() const {
			return (FlexJustify)YGNodeStyleGetJustifyContent(_yogaNode);
		}

		void setFlexAlignItems(FlexAlign align) {
			YGNodeStyleSetAlignItems(_yogaNode, (YGAlign)align);
			_dirty = true;
		}

		FlexAlign getFlexAlignItems() const {
			return (FlexAlign)YGNodeStyleGetAlignItems(_yogaNode);
		}

		void setFlexAlignSelf(FlexAlign align) {
			YGNodeStyleSetAlignSelf(_yogaNode, (YGAlign)align);
			_dirty = true;
		}

		FlexAlign getFlexAlignSelf() const {
			return (FlexAlign)YGNodeStyleGetAlignSelf(_yogaNode);
		}

		void setFlexAlignContent(FlexAlign align) {
			YGNodeStyleSetAlignContent(_yogaNode, (YGAlign)align);
			_dirty = true;
		}

		FlexAlign getFlexAlignContent() const {
			return (FlexAlign)YGNodeStyleGetAlignContent(_yogaNode);
		}

		void setLayoutDirection(LayoutDirection layoutDirection) {
			YGNodeStyleSetDirection(_yogaNode, (YGDirection)layoutDirection);
			_dirty = true;
		}

		LayoutDirection getLayoutDirection() const {
			return (LayoutDirection)YGNodeStyleGetDirection(_yogaNode);
		}

		void setFlexDirection(FlexDirection flexDirection) {
			YGNodeStyleSetFlexDirection(_yogaNode, (YGFlexDirection)flexDirection);
			_dirty = true;
		}

		FlexDirection getFlexDirection() const {
			return (FlexDirection)YGNodeStyleGetFlexDirection(_yogaNode);
		}

		void setFlexWrap(FlexWrap flexWrap) {
			YGNodeStyleSetFlexWrap(_yogaNode, (YGWrap)flexWrap);
			_dirty = true;
		}

		FlexWrap getFlexWrap() const {
			return (FlexWrap)YGNodeStyleGetFlexWrap(_yogaNode);
		}

		void setFlexGrow(f32 grow) {
			YGNodeStyleSetFlexGrow(_yogaNode, grow);
			_dirty = true;
		}

		f32 getFlexGrow() const {
			return YGNodeStyleGetFlexGrow(_yogaNode);
		}

		void setFlexShrink(f32 shrink) {
			YGNodeStyleSetFlexShrink(_yogaNode, shrink);
			_dirty = true;
		}

		f32 getFlexShrink() const {
			return YGNodeStyleGetFlexShrink(_yogaNode);
		}

		FlexValue getMinHeight() const {
			auto value = YGNodeStyleGetMinHeight(_yogaNode);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMinHeight(FlexValue min) {
			switch (min.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMinHeight(_yogaNode, min.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMinHeightPercent(_yogaNode, min.getValue()); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getMaxHeight() const {
			auto value = YGNodeStyleGetMaxHeight(_yogaNode);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMaxHeight(FlexValue max) {
			switch (max.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMaxHeight(_yogaNode, max.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMaxHeightPercent(_yogaNode, max.getValue()); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getMinWidth() const {
			auto value = YGNodeStyleGetMinWidth(_yogaNode);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMinWidth(FlexValue min) {
			switch (min.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMinWidth(_yogaNode, min.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMinWidthPercent(_yogaNode, min.getValue()); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getMaxWidth() const {
			auto value = YGNodeStyleGetMaxWidth(_yogaNode);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMaxWidth(FlexValue max) {
			switch (max.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMaxWidth(_yogaNode, max.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMaxWidthPercent(_yogaNode, max.getValue()); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getWidth() const {
			auto value = YGNodeStyleGetWidth(_yogaNode);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setWidth(FlexValue min) {
			switch (min.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetWidth(_yogaNode, min.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetWidthPercent(_yogaNode, min.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetWidthAuto(_yogaNode); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getHeight() const {
			auto value = YGNodeStyleGetHeight(_yogaNode);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setHeight(FlexValue min) {
			switch (min.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetHeight(_yogaNode, min.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetHeightPercent(_yogaNode, min.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetHeightAuto(_yogaNode); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		void setAspectRatio(f32 ratio) {
			YGNodeStyleSetAspectRatio(_yogaNode, ratio);
			_dirty = true;
		}

		f32 getAspectRatio() const {
			return YGNodeStyleGetAspectRatio(_yogaNode);
		}

		void setFlexPositionType(FlexPositionType positionType) {
			YGNodeStyleSetPositionType(_yogaNode, (YGPositionType)positionType);
			_dirty = true;
		}

		void setPaddingEdge(FlexEdge edge, FlexValue value) {
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetPadding(_yogaNode, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetPaddingPercent(_yogaNode, (YGEdge)edge, value.getValue()); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getPaddingEdge(FlexEdge edge) const {
			auto value = YGNodeStyleGetPadding(_yogaNode, (YGEdge)edge);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setPadding(const FlexRect& rect) {
			setPaddingEdge(FlexEdge::Top, rect.top);
			setPaddingEdge(FlexEdge::Left, rect.left);
			setPaddingEdge(FlexEdge::Bottom, rect.bottom);
			setPaddingEdge(FlexEdge::Right, rect.right);
		}

		FlexRect getPadding() const {
			return FlexRect{
				getPaddingEdge(FlexEdge::Top),
				getPaddingEdge(FlexEdge::Left),
				getPaddingEdge(FlexEdge::Bottom),
				getPaddingEdge(FlexEdge::Right)
			};
		}

		void setBorderEdge(FlexEdge edge, f32 value) {
			YGNodeStyleSetBorder(_yogaNode, (YGEdge)edge, value);
			_dirty = true;
		}

		f32 getBorderEdge(FlexEdge edge) const {
			return YGNodeStyleGetBorder(_yogaNode, (YGEdge)edge);
		}

		void setBorder(const FlexBorder& rect) {
			setBorderEdge(FlexEdge::Top, rect.top);
			setBorderEdge(FlexEdge::Left, rect.left);
			setBorderEdge(FlexEdge::Bottom, rect.bottom);
			setBorderEdge(FlexEdge::Right, rect.right);
		}

		FlexBorder getBorder() const {
			return FlexBorder{
				getBorderEdge(FlexEdge::Top),
				getBorderEdge(FlexEdge::Left),
				getBorderEdge(FlexEdge::Bottom),
				getBorderEdge(FlexEdge::Right)
			};
		}

		void setPositionEdge(FlexEdge edge, FlexValue value) {
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetPosition(_yogaNode, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetPositionPercent(_yogaNode, (YGEdge)edge, value.getValue()); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getPositionEdge(FlexEdge edge) const {
			YGValue value = YGNodeStyleGetPosition(_yogaNode, (YGEdge)edge);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setPosition(const FlexRect& rect) {
			setPositionEdge(FlexEdge::Top, rect.top);
			setPositionEdge(FlexEdge::Left, rect.left);
			setPositionEdge(FlexEdge::Bottom, rect.bottom);
			setPositionEdge(FlexEdge::Right, rect.right);
		}

		FlexRect getPosition() const {
			return FlexRect{
				getPositionEdge(FlexEdge::Top),
				getPositionEdge(FlexEdge::Left),
				getPositionEdge(FlexEdge::Bottom),
				getPositionEdge(FlexEdge::Right)
			};
		}

		void setMarginEdge(FlexEdge edge, FlexValue value) {
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMargin(_yogaNode, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMarginPercent(_yogaNode, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetMarginAuto(_yogaNode, (YGEdge)edge); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getMarginEdge(FlexEdge edge) const {
			YGValue value = YGNodeStyleGetMargin(_yogaNode, (YGEdge)edge);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMargin(const FlexRect& rect) {
			setMarginEdge(FlexEdge::Top, rect.top);
			setMarginEdge(FlexEdge::Left, rect.left);
			setMarginEdge(FlexEdge::Bottom, rect.bottom);
			setMarginEdge(FlexEdge::Right, rect.right);
		}

		void setMarginValue(FlexValue value) {
			setMarginEdge(FlexEdge::Top, value);
			setMarginEdge(FlexEdge::Left, value);
			setMarginEdge(FlexEdge::Bottom, value);
			setMarginEdge(FlexEdge::Right, value);
		}

		FlexRect getMargin() const {
			return FlexRect{
				getMarginEdge(FlexEdge::Top),
				getMarginEdge(FlexEdge::Left),
				getMarginEdge(FlexEdge::Bottom),
				getMarginEdge(FlexEdge::Right)
			};
		}

		void setPaddingValue(FlexValue value) {
			setPaddingEdge(FlexEdge::Top, value);
			setPaddingEdge(FlexEdge::Left, value);
			setPaddingEdge(FlexEdge::Bottom, value);
			setPaddingEdge(FlexEdge::Right, value);
		}

		void setFlexBasis(FlexValue value) {
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetFlexBasis(_yogaNode, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetFlexBasisPercent(_yogaNode, value.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetFlexBasisAuto(_yogaNode); break;
			//default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getFlexBasis() const {
			YGValue value = YGNodeStyleGetFlexBasis(_yogaNode);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMinDimensions(FlexDimensionValue min) {
			setMinWidth(min.width);
			setMinHeight(min.height);
		}

		void setMaxDimensions(FlexDimensionValue max) {
			setMaxWidth(max.width);
			setMaxHeight(max.height);
		}

		void setDimensions(FlexDimensionValue dimensions) {
			setWidth(dimensions.width);
			setHeight(dimensions.height);
		}

		void setDimensions(FlexValue dimensions) {
			setWidth(dimensions);
			setHeight(dimensions);
		}

		void setDimensions(Dimension dimensions) {
			setDimensions(FlexDimensionValue{
				(f32)dimensions.w,
				(f32)dimensions.h
			});
		}
		
		PointF getCalculatedPosition() const {
			return PointF(YGNodeLayoutGetLeft(_yogaNode), YGNodeLayoutGetTop(_yogaNode));
		}
		
		DimensionF getCalculatedDimensions() const {
			return DimensionF(YGNodeLayoutGetWidth(_yogaNode), YGNodeLayoutGetHeight(_yogaNode));
		}

		RectF getCalculatedArea() const {
			return RectF{
				getCalculatedPosition(),
				getCalculatedDimensions()
			};
		}
		
		PointF getWorldPosition() {
			YGNodeRef parent = YGNodeGetParent(_yogaNode);
			if (parent) {
				return getCalculatedPosition() + PointF(YGNodeLayoutGetLeft(parent), YGNodeLayoutGetTop(parent));
			}

			return getCalculatedPosition();
		}

		RectF getWorldArea() {
			return RectF{
				getWorldPosition(),
				getCalculatedDimensions()
			};
		}

		FlexBorder getComputedPadding() const {
			return FlexBorder{
				YGNodeLayoutGetPadding(_yogaNode, YGEdge::YGEdgeTop),
				YGNodeLayoutGetPadding(_yogaNode, YGEdge::YGEdgeLeft),
				YGNodeLayoutGetPadding(_yogaNode, YGEdge::YGEdgeBottom),
				YGNodeLayoutGetPadding(_yogaNode, YGEdge::YGEdgeRight)
			};
		}

		FlexBorder getComputedMargin() const {
			return FlexBorder{
				YGNodeLayoutGetMargin(_yogaNode, YGEdge::YGEdgeTop),
				YGNodeLayoutGetMargin(_yogaNode, YGEdge::YGEdgeLeft),
				YGNodeLayoutGetMargin(_yogaNode, YGEdge::YGEdgeBottom),
				YGNodeLayoutGetMargin(_yogaNode, YGEdge::YGEdgeRight)
			};
		}
		
		YGNodeRef getNode() const {
			return _yogaNode;
		}
	};
}

REFL_AUTO(
	type(fw::ViewLayout),
	
	func(getFlexDirection,		property("flexDirection")),		func(setFlexDirection,		property("flexDirection")),
	func(getJustifyContent,		property("justifyContent")),	func(setJustifyContent,		property("justifyContent")),
	func(getFlexAlignItems,		property("flexAlignItems")),	func(setFlexAlignItems,		property("flexAlignItems")),
	func(getFlexAlignSelf,		property("flexAlignSelf")),		func(setFlexAlignSelf,		property("flexAlignSelf")),
	func(getFlexAlignContent,	property("flexAlignContent")),	func(setFlexAlignContent,	property("flexAlignContent")),
	func(getLayoutDirection,	property("layoutDirection")),	func(setLayoutDirection,	property("layoutDirection")),
	func(getFlexWrap,			property("flexWrap")),			func(setFlexWrap,			property("flexWrap")),
	func(getFlexGrow,			property("flexGrow")),			func(setFlexGrow,			property("flexGrow")),
	func(getFlexShrink,			property("flexShrink")),		func(setFlexShrink,			property("flexShrink")),
	func(getFlexBasis,			property("flexBasis")),			func(setFlexBasis,			property("flexBasis")),
	func(getMinWidth,			property("minWidth")),			func(setMinWidth,			property("minWidth")),
	func(getMaxWidth,			property("maxWidth")),			func(setMaxWidth,			property("maxWidth")),
	func(getMinHeight,			property("minHeight")),			func(setMinHeight,			property("minHeight")),
	func(getMaxHeight,			property("maxHeight")),			func(setMaxHeight,			property("maxHeight")),
	func(getWidth,				property("width")),				func(setWidth,				property("width")),
	func(getHeight,				property("height")),			func(setHeight,				property("height")),
	func(getAspectRatio,		property("aspectRatio")),		func(setAspectRatio,		property("aspectRatio")),
	func(getPosition,			property("position")),			func(setPosition,			property("position")),
	func(getPadding,			property("padding")),			func(setPadding,			property("padding")),
	func(getMargin,				property("margin")),			func(setMargin,				property("margin")),
	func(getBorder,				property("border")),			func(setBorder,				property("border")),
	func(getOverflow,			property("overflow")),			func(setOverflow,			property("overflow"))
)
