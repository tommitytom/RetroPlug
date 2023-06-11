#pragma once

#include <refl.hpp>
#include <yoga/Yoga.h>

#include "ui/Flex.h"

namespace fw {
	class ViewLayout {
	private:
		YGNodeRef _yogaNode = nullptr;
		bool _dirty = false;
		
	public:
		ViewLayout() : _yogaNode(YGNodeNew()) {

		}

		ViewLayout(Dimension dimensions) : _yogaNode(YGNodeNew()) {
			setDimensions(dimensions);
			YGNodeCalculateLayout(_yogaNode, YGUndefined, YGUndefined, YGDirectionInherit);
		}

		~ViewLayout() {
			YGNodeFree(_yogaNode);
		}
		
		ViewLayout& operator=(ViewLayout&& other) noexcept {
			if (_yogaNode) {
				YGNodeFree(_yogaNode);
			}
			
			_yogaNode = other._yogaNode;
			_dirty = true;
			
			other._yogaNode = nullptr;
			other._dirty = false;

			return *this;
		}

		ViewLayout& operator=(const ViewLayout&) = delete;

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
			default: assert(false);
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
			default: assert(false);
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
			default: assert(false);
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
			default: assert(false);
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
			default: assert(false);
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
			default: assert(false);
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

		void setPadding(FlexEdge edge, FlexValue value) {
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetPadding(_yogaNode, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetPaddingPercent(_yogaNode, (YGEdge)edge, value.getValue()); break;
			default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getPadding(FlexEdge edge) const {
			auto value = YGNodeStyleGetPadding(_yogaNode, (YGEdge)edge);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setPadding(const FlexRect& rect) {
			setPadding(FlexEdge::Top, rect.top);
			setPadding(FlexEdge::Left, rect.left);
			setPadding(FlexEdge::Bottom, rect.bottom);
			setPadding(FlexEdge::Right, rect.right);
		}

		FlexRect getPadding() const {
			return FlexRect{
				getPadding(FlexEdge::Top),
				getPadding(FlexEdge::Left),
				getPadding(FlexEdge::Bottom),
				getPadding(FlexEdge::Right)
			};
		}

		void setBorder(FlexEdge edge, f32 value) {
			YGNodeStyleSetBorder(_yogaNode, (YGEdge)edge, value);
			_dirty = true;
		}

		f32 getBorder(FlexEdge edge) const {
			return YGNodeStyleGetBorder(_yogaNode, (YGEdge)edge);
		}

		void setBorder(const FlexBorder& rect) {
			setBorder(FlexEdge::Top, rect.top);
			setBorder(FlexEdge::Left, rect.left);
			setBorder(FlexEdge::Bottom, rect.bottom);
			setBorder(FlexEdge::Right, rect.right);
		}

		FlexBorder getBorder() const {
			return FlexBorder{
				getBorder(FlexEdge::Top),
				getBorder(FlexEdge::Left),
				getBorder(FlexEdge::Bottom),
				getBorder(FlexEdge::Right)
			};
		}

		void setPosition(FlexEdge edge, FlexValue value) {
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetPosition(_yogaNode, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetPositionPercent(_yogaNode, (YGEdge)edge, value.getValue()); break;
			default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getPosition(FlexEdge edge) const {
			YGValue value = YGNodeStyleGetPosition(_yogaNode, (YGEdge)edge);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setPosition(const FlexRect& rect) {
			setPosition(FlexEdge::Top, rect.top);
			setPosition(FlexEdge::Left, rect.left);
			setPosition(FlexEdge::Bottom, rect.bottom);
			setPosition(FlexEdge::Right, rect.right);
		}

		FlexRect getPosition() const {
			return FlexRect{
				getPosition(FlexEdge::Top),
				getPosition(FlexEdge::Left),
				getPosition(FlexEdge::Bottom),
				getPosition(FlexEdge::Right)
			};
		}

		void setMargin(FlexEdge edge, FlexValue value) {
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMargin(_yogaNode, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMarginPercent(_yogaNode, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetMarginAuto(_yogaNode, (YGEdge)edge); break;
			default: assert(false);
			}

			_dirty = true;
		}

		FlexValue getMargin(FlexEdge edge) const {
			YGValue value = YGNodeStyleGetMargin(_yogaNode, (YGEdge)edge);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMargin(const FlexRect& rect) {
			setMargin(FlexEdge::Top, rect.top);
			setMargin(FlexEdge::Left, rect.left);
			setMargin(FlexEdge::Bottom, rect.bottom);
			setMargin(FlexEdge::Right, rect.right);
		}

		FlexRect getMargin() const {
			return FlexRect{
				getMargin(FlexEdge::Top),
				getMargin(FlexEdge::Left),
				getMargin(FlexEdge::Bottom),
				getMargin(FlexEdge::Right)
			};
		}

		void setFlexBasis(FlexValue value) {
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetFlexBasis(_yogaNode, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetFlexBasisPercent(_yogaNode, value.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetFlexBasisAuto(_yogaNode); break;
			default: assert(false);
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

		void setFlex(Flex&& flex) {
			if (flex.direction.has_value()) setLayoutDirection(*flex.direction);
			if (flex.flexDirection.has_value()) setFlexDirection(*flex.flexDirection);
			if (flex.basis.has_value()) setFlexBasis(*flex.basis);
			if (flex.grow.has_value()) setFlexGrow(*flex.grow);
			if (flex.shrink.has_value()) setFlexShrink(*flex.shrink);
			if (flex.flexWrap.has_value()) setFlexWrap(*flex.flexWrap);

			if (flex.justify.has_value()) setJustifyContent(*flex.justify);
			if (flex.alignItems.has_value()) setFlexAlignItems(*flex.alignItems);
			if (flex.alignSelf.has_value()) setFlexAlignSelf(*flex.alignSelf);
			if (flex.alignContent.has_value()) setFlexAlignContent(*flex.alignContent);
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

		YGNodeRef getNode() const {
			return _yogaNode;
		}
	};
}

REFL_AUTO(
	type(fw::ViewLayout),
	
	func(getFlexDirection, property("flexDirection")), func(setFlexDirection, property("flexDirection")),
	func(getJustifyContent, property("justifyContent")), func(setJustifyContent, property("justifyContent")),
	func(getFlexAlignItems, property("flexAlignItems")), func(setFlexAlignItems, property("flexAlignItems")),
	func(getFlexAlignSelf, property("flexAlignSelf")), func(setFlexAlignSelf, property("flexAlignSelf")),
	func(getFlexAlignContent, property("flexAlignContent")), func(setFlexAlignContent, property("flexAlignContent")),
	func(getLayoutDirection, property("layoutDirection")), func(setLayoutDirection, property("layoutDirection")),
	func(getFlexWrap, property("flexWrap")), func(setFlexWrap, property("flexWrap")),
	func(getFlexGrow, property("flexGrow")), func(setFlexGrow, property("flexGrow")),
	func(getFlexShrink, property("flexShrink")), func(setFlexShrink, property("flexShrink")),
	func(getFlexBasis, property("flexBasis")), func(setFlexBasis, property("flexBasis")),
	func(getMinWidth, property("minWidth")), func(setMinWidth, property("minWidth")),
	func(getMaxWidth, property("maxWidth")), func(setMaxWidth, property("maxWidth")),
	func(getMinHeight, property("minHeight")), func(setMinHeight, property("minHeight")),
	func(getMaxHeight, property("maxHeight")), func(setMaxHeight, property("maxHeight")),
	func(getWidth, property("width")), func(setWidth, property("width")),
	func(getHeight, property("height")), func(setHeight, property("height")),
	func(getAspectRatio, property("aspectRatio")), func(setAspectRatio, property("aspectRatio")),
	func(getPosition, property("position")), func(setPosition, property("position")),
	func(getPadding, property("padding")), func(setPadding, property("padding")),
	func(getMargin, property("margin")), func(setMargin, property("margin")),
	func(getBorder, property("border")), func(setBorder, property("border"))
)
