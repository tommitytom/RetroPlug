#pragma once

#include <entt/entity/registry.hpp>
#include <entt/core/enum.hpp>
#include <yoga/Yoga.h>
#include <spdlog/spdlog.h>

#include "foundation/Math.h"
#include "foundation/Types.h"
#include "ui/Flex.h"
#include "ui/next/DocumentTypes.h"
#include "ui/next/StyleComponents.h"

//struct YGNode;
//typedef struct YGNode* YGNodeRef;

namespace fw {
	class DomStyle {
	private:
		entt::registry& _reg;
		entt::entity _entity;
		YGNodeRef _node;

	public:
		DomStyle(entt::registry& reg, entt::entity e, YGNodeRef node);
		DomStyle(DomStyle& other): _reg(other._reg), _entity(other._entity), _node(other._node) {}
		DomStyle(DomStyle&& other) noexcept : _reg(other._reg), _entity(other._entity), _node(other._node) {
			other._entity = entt::null;
			other._node = nullptr;
		}

		~DomStyle() = default;

		void setColor(const Color4F& color) {
			_reg.emplace_or_replace<ColorStyle>(_entity, ColorStyle{ color });
		}

		const Color4F& getColor() const {
			return _reg.get<ColorStyle>(_entity).color;
		}

		void setBackgroundColor(const Color4F& color) {
			_reg.emplace_or_replace<BackgroundColorStyle>(_entity, BackgroundColorStyle{ color });
		}

		const Color4F& getBackgroundColor() const {
			return _reg.get<BackgroundColorStyle>(_entity).color;
		}

		const std::string& getNodeValue() const {
			return _reg.get<TextComponent>(_entity).text;
		}

		void setNodeValue(const std::string& value) {
			_reg.get<TextComponent>(_entity).text = value;
		}

		template <typename T>
		void updateOrRemove(T* value) {
			if (value) {
				//_reg.emplace_or_replace<T>(node, std::forward<T>(value.value()));
				_reg.emplace_or_replace<T>(_node, *value);
			} else {
				_reg.remove<T>(_node);
			}
		}

		/*template <typename T>
		const T* tryGet(entt::entity node) const {
			return _reg.try_get<const T>(node);
		}*/

		template <typename T>
		T* tryGet() {
			return _reg.try_get<T>(_node);
		}
		
		void toggleEventFlags(EventFlag flag, bool on) {
			EventFlag& v = _reg.get<EventFlag>(_entity);

			if (on) {
				v |= flag;
			} else {
				v &= ~flag;
			}
		}

		void toggleStyleFlag(FlexStyleFlag flag, bool on) {
			FlexStyleFlag& v = _reg.get<FlexStyleFlag>(_entity);
			
			if (on) {
				v |= flag;
			} else {
				v &= ~flag;
			}
		}

		bool hasStyleFlag(FlexStyleFlag flag) const {
			return !!(_reg.get<FlexStyleFlag>(_entity) & flag);
		}

		bool hasEventFlag(EventFlag flag) const {
			return !!(_reg.get<EventFlag>(_entity) & flag);
		}

		void setOverflow(FlexOverflow overflow) {
			toggleStyleFlag(FlexStyleFlag::Overflow, true);
			YGNodeStyleSetOverflow(_node, (YGOverflow)overflow);
		}

		FlexOverflow getOverflow() const {
			return (FlexOverflow)YGNodeStyleGetOverflow(_node);
		}

		void setJustifyContent(FlexJustify justify) {
			toggleStyleFlag(FlexStyleFlag::JustifyContent, true);
			YGNodeStyleSetJustifyContent(_node, (YGJustify)justify);
		}

		FlexJustify getJustifyContent() const {
			return (FlexJustify)YGNodeStyleGetJustifyContent(_node);
		}

		void setFlexAlignItems(FlexAlign align) {
			toggleStyleFlag(FlexStyleFlag::FlexAlignItems, true);
			YGNodeStyleSetAlignItems(_node, (YGAlign)align);
		}

		FlexAlign getFlexAlignItems() const {
			return (FlexAlign)YGNodeStyleGetAlignItems(_node);
		}

		void setFlexAlignSelf(FlexAlign align) {
			toggleStyleFlag(FlexStyleFlag::FlexAlignSelf, true);
			YGNodeStyleSetAlignSelf(_node, (YGAlign)align);
		}

		FlexAlign getFlexAlignSelf() const {
			return (FlexAlign)YGNodeStyleGetAlignSelf(_node);
		}

		void setFlexAlignContent(FlexAlign align) {
			toggleStyleFlag(FlexStyleFlag::FlexAlignContent, true);
			YGNodeStyleSetAlignContent(_node, (YGAlign)align);
		}

		FlexAlign getFlexAlignContent() const {
			return (FlexAlign)YGNodeStyleGetAlignContent(_node);
		}

		void setLayoutDirection(LayoutDirection layoutDirection) {
			toggleStyleFlag(FlexStyleFlag::LayoutDirection, true);
			YGNodeStyleSetDirection(_node, (YGDirection)layoutDirection);
		}

		LayoutDirection getLayoutDirection() const {
			return (LayoutDirection)YGNodeStyleGetDirection(_node);
		}

		void setFlexDirection(FlexDirection flexDirection) {
			toggleStyleFlag(FlexStyleFlag::FlexDirection, true);
			YGNodeStyleSetFlexDirection(_node, (YGFlexDirection)flexDirection);
		}

		FlexDirection getFlexDirection() const {
			return (FlexDirection)YGNodeStyleGetFlexDirection(_node);
		}

		void setFlexWrap(FlexWrap flexWrap) {
			toggleStyleFlag(FlexStyleFlag::FlexWrap, true);
			YGNodeStyleSetFlexWrap(_node, (YGWrap)flexWrap);
		}

		FlexWrap getFlexWrap() const {
			return (FlexWrap)YGNodeStyleGetFlexWrap(_node);
		}

		void setFlexGrow(f32 grow) {
			toggleStyleFlag(FlexStyleFlag::FlexGrow, true);
			YGNodeStyleSetFlexGrow(_node, grow);
		}

		f32 getFlexGrow() const {
			return YGNodeStyleGetFlexGrow(_node);
		}

		void setFlexShrink(f32 shrink) {
			toggleStyleFlag(FlexStyleFlag::FlexShrink, true);
			YGNodeStyleSetFlexShrink(_node, shrink);
		}

		f32 getFlexShrink() const {
			return YGNodeStyleGetFlexShrink(_node);
		}

		FlexValue getMinHeight() const {
			auto value = YGNodeStyleGetMinHeight(_node);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMinHeight(FlexValue min) {
			toggleStyleFlag(FlexStyleFlag::MinHeight, true);
			switch (min.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMinHeight(_node, min.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMinHeightPercent(_node, min.getValue()); break;
				//default: assert(false);
			}
		}

		FlexValue getMaxHeight() const {
			auto value = YGNodeStyleGetMaxHeight(_node);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMaxHeight(FlexValue max) {
			toggleStyleFlag(FlexStyleFlag::MaxHeight, true);
			switch (max.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMaxHeight(_node, max.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMaxHeightPercent(_node, max.getValue()); break;
				//default: assert(false);
			}
		}

		FlexValue getMinWidth() const {
			auto value = YGNodeStyleGetMinWidth(_node);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMinWidth(FlexValue min) {
			toggleStyleFlag(FlexStyleFlag::MinWidth, true);
			switch (min.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMinWidth(_node, min.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMinWidthPercent(_node, min.getValue()); break;
				//default: assert(false);
			}
		}

		FlexValue getMaxWidth() const {
			auto value = YGNodeStyleGetMaxWidth(_node);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMaxWidth(FlexValue max) {
			toggleStyleFlag(FlexStyleFlag::MaxWidth, true);
			switch (max.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetMaxWidth(_node, max.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMaxWidthPercent(_node, max.getValue()); break;
				//default: assert(false);
			}

		}

		FlexValue getWidth() const {
			auto value = YGNodeStyleGetWidth(_node);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setWidth(FlexValue min) {
			toggleStyleFlag(FlexStyleFlag::Width, true);
			switch (min.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetWidth(_node, min.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetWidthPercent(_node, min.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetWidthAuto(_node); break;
				//default: assert(false);
			}

		}

		FlexValue getHeight() const {
			auto value = YGNodeStyleGetHeight(_node);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setHeight(FlexValue min) {
			toggleStyleFlag(FlexStyleFlag::Height, true);
			switch (min.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetHeight(_node, min.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetHeightPercent(_node, min.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetHeightAuto(_node); break;
				//default: assert(false);
			}
		}

		void setAspectRatio(f32 ratio) {
			toggleStyleFlag(FlexStyleFlag::AspectRatio, true);
			YGNodeStyleSetAspectRatio(_node, ratio);
		}

		f32 getAspectRatio() const {
			return YGNodeStyleGetAspectRatio(_node);
		}

		void setFlexPositionType(FlexPositionType positionType) {
			toggleStyleFlag(FlexStyleFlag::FlexPositionType, true);
			YGNodeStyleSetPositionType(_node, (YGPositionType)positionType);
		}
		
		FlexPositionType getFlexPositionType() const {
			return (FlexPositionType)YGNodeStyleGetPositionType(_node);
		}

		void setPaddingEdge(FlexEdge edge, FlexValue value) {
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetPadding(_node, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetPaddingPercent(_node, (YGEdge)edge, value.getValue()); break;
				//default: assert(false);
			}
		}

		FlexValue getPaddingEdge(FlexEdge edge) const {
			auto value = YGNodeStyleGetPadding(_node, (YGEdge)edge);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setPadding(const FlexRect& rect) {
			toggleStyleFlag(FlexStyleFlag::Padding, true);
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
			YGNodeStyleSetBorder(_node, (YGEdge)edge, value);
		}

		f32 getBorderEdge(FlexEdge edge) const {
			return YGNodeStyleGetBorder(_node, (YGEdge)edge);
		}

		void setBorder(const FlexBorder& rect) {
			toggleStyleFlag(FlexStyleFlag::Border, true);
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
			case FlexUnit::Point: YGNodeStyleSetPosition(_node, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetPositionPercent(_node, (YGEdge)edge, value.getValue()); break;
				//default: assert(false);
			}
		}

		FlexValue getPositionEdge(FlexEdge edge) const {
			YGValue value = YGNodeStyleGetPosition(_node, (YGEdge)edge);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setPosition(const FlexRect& rect) {
			toggleStyleFlag(FlexStyleFlag::Position, true);
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
			case FlexUnit::Point: YGNodeStyleSetMargin(_node, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetMarginPercent(_node, (YGEdge)edge, value.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetMarginAuto(_node, (YGEdge)edge); break;
				//default: assert(false);
			}
		}

		FlexValue getMarginEdge(FlexEdge edge) const {
			YGValue value = YGNodeStyleGetMargin(_node, (YGEdge)edge);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		void setMargin(const FlexRect& rect) {
			toggleStyleFlag(FlexStyleFlag::Margin, true);
			setMarginEdge(FlexEdge::Top, rect.top);
			setMarginEdge(FlexEdge::Left, rect.left);
			setMarginEdge(FlexEdge::Bottom, rect.bottom);
			setMarginEdge(FlexEdge::Right, rect.right);
		}

		FlexRect getMargin() const {
			return FlexRect{
				getMarginEdge(FlexEdge::Top),
				getMarginEdge(FlexEdge::Left),
				getMarginEdge(FlexEdge::Bottom),
				getMarginEdge(FlexEdge::Right)
			};
		}

		void setFlexBasis(FlexValue value) {
			toggleStyleFlag(FlexStyleFlag::FlexBasis, true);
			switch (value.getUnit()) {
			case FlexUnit::Point: YGNodeStyleSetFlexBasis(_node, value.getValue()); break;
			case FlexUnit::Percent: YGNodeStyleSetFlexBasisPercent(_node, value.getValue()); break;
			case FlexUnit::Auto: YGNodeStyleSetFlexBasisAuto(_node); break;
				//default: assert(false);
			}
		}

		FlexValue getFlexBasis() const {
			YGValue value = YGNodeStyleGetFlexBasis(_node);
			return FlexValue((FlexUnit)value.unit, value.value);
		}

		PointF getCalculatedPosition() const {
			return PointF(YGNodeLayoutGetLeft(_node), YGNodeLayoutGetTop(_node));
		}

		DimensionF getCalculatedDimensions() const {
			return DimensionF(YGNodeLayoutGetWidth(_node), YGNodeLayoutGetHeight(_node));
		}

		RectF getCalculatedArea() const {
			return RectF{
				getCalculatedPosition(),
				getCalculatedDimensions()
			};
		}

		FlexBorder getComputedPadding() const {
			return FlexBorder{
				YGNodeLayoutGetPadding(_node, YGEdge::YGEdgeTop),
				YGNodeLayoutGetPadding(_node, YGEdge::YGEdgeLeft),
				YGNodeLayoutGetPadding(_node, YGEdge::YGEdgeBottom),
				YGNodeLayoutGetPadding(_node, YGEdge::YGEdgeRight)
			};
		}

		FlexBorder getComputedMargin() const {
			return FlexBorder{
				YGNodeLayoutGetMargin(_node, YGEdge::YGEdgeTop),
				YGNodeLayoutGetMargin(_node, YGEdge::YGEdgeLeft),
				YGNodeLayoutGetMargin(_node, YGEdge::YGEdgeBottom),
				YGNodeLayoutGetMargin(_node, YGEdge::YGEdgeRight)
			};
		}

		YGNodeRef getNode() const {
			return _node;
		}
	};
}
