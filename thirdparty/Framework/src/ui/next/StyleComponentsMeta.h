#pragma once

#include "foundation/MathMeta.h"
#include "StyleComponents.h"
#include "DomStyle.h"

#include <lua.hpp>
#include <LuaBridge.h>
#include "ui/Flex.h"
#include <entt/entity/entity.hpp>
#include "foundation/Input.h"

template <> struct luabridge::Stack<fw::MouseButton> : luabridge::Enum<fw::MouseButton> {};
template <> struct luabridge::Stack<fw::ButtonType> : luabridge::Enum<fw::ButtonType> {};

template <> struct luabridge::Stack<fw::FlexAlign> : luabridge::Enum<fw::FlexAlign> {};
template <> struct luabridge::Stack<fw::FlexDimension> : luabridge::Enum<fw::FlexDimension> {};
template <> struct luabridge::Stack<fw::LayoutDirection> : luabridge::Enum<fw::LayoutDirection> {};
template <> struct luabridge::Stack<fw::FlexDisplay> : luabridge::Enum<fw::FlexDisplay> {};
template <> struct luabridge::Stack<fw::FlexEdge> : luabridge::Enum<fw::FlexEdge> {};
template <> struct luabridge::Stack<fw::FlexDirection> : luabridge::Enum<fw::FlexDirection> {};
template <> struct luabridge::Stack<fw::FlexGutter> : luabridge::Enum<fw::FlexGutter> {};
template <> struct luabridge::Stack<fw::FlexJustify> : luabridge::Enum<fw::FlexJustify> {};
template <> struct luabridge::Stack<fw::FlexMeasureMode> : luabridge::Enum<fw::FlexMeasureMode> {};
template <> struct luabridge::Stack<fw::FlexNodeType> : luabridge::Enum<fw::FlexNodeType> {};
template <> struct luabridge::Stack<fw::FlexOverflow> : luabridge::Enum<fw::FlexOverflow> {};
template <> struct luabridge::Stack<fw::FlexPositionType> : luabridge::Enum<fw::FlexPositionType> {};
template <> struct luabridge::Stack<fw::FlexUnit> : luabridge::Enum<fw::FlexUnit> {};
template <> struct luabridge::Stack<fw::FlexWrap> : luabridge::Enum<fw::FlexWrap> {};

template <> struct luabridge::Stack<entt::entity> : luabridge::Enum<entt::entity> {};

REFL_AUTO(
	type(fw::TextComponent),
	field(text)
	//field(face)
)

REFL_AUTO(
	type(fw::TextureComponent)
	//field(texture)
)

REFL_AUTO(
	type(fw::DomStyle),

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
	func(getBorder, property("border")), func(setBorder, property("border")),
	func(getOverflow, property("overflow")), func(setOverflow, property("overflow"))
)

struct lua_State;

namespace fw::LuaUtil {
	void reflectStyleComponents(lua_State* lua);
}
