#pragma once

#include "StyleComponentsMeta.h"
#include "foundation/LuaReflection.h"
#include "ui/Flex.h"
#include "ui/View.h"
#include "ui/next/Document.h"
//#include "ui/ViewLayout.h"

namespace fw {
	void LuaUtil::reflectStyleComponents(lua_State* lua) {
		//LuaReflection::addClass<FlexValue>(lua);
		//LuaReflection::addClass<Color4F>(lua);

		LuaReflection::addEnum<MouseButton>(lua);
		LuaReflection::addEnum<ButtonType>(lua);
		LuaReflection::addEnum<VirtualKey>(lua);		
		LuaReflection::addEnum<KeyAction>(lua);

		LuaReflection::addEnum<FlexAlign>(lua);
		LuaReflection::addEnum<FlexDimension>(lua);
		LuaReflection::addEnum<FlexDirection>(lua);
		LuaReflection::addEnum<LayoutDirection>(lua);
		LuaReflection::addEnum<FlexDisplay>(lua);
		LuaReflection::addEnum<FlexEdge>(lua);
		LuaReflection::addEnum<FlexGutter>(lua);
		LuaReflection::addEnum<FlexJustify>(lua);
		LuaReflection::addEnum<FlexMeasureMode>(lua);
		LuaReflection::addEnum<FlexNodeType>(lua);
		LuaReflection::addEnum<FlexOverflow>(lua);
		LuaReflection::addEnum<FlexPositionType>(lua);
		LuaReflection::addEnum<FlexUnit>(lua);
		LuaReflection::addEnum<FlexWrap>(lua);
		LuaReflection::addEnum<CursorType>(lua);

		LuaReflection::addEnum<TransitionTimingType>(lua);
		LuaReflection::addEnum<FontStyleType>(lua);
		LuaReflection::addEnum<FontWeightType>(lua);
		LuaReflection::addEnum<FontGenericType>(lua);
		LuaReflection::addEnum<LengthType>(lua);
		LuaReflection::addEnum<TextAlignType>(lua);
		LuaReflection::addEnum<BorderStyleType>(lua);

		LuaReflection::addClass<ViewLayout>(lua);

		LuaReflection::addClass<PointI32>(lua);
		LuaReflection::addClass<PointF32>(lua);
		LuaReflection::addClass<MouseMoveEvent>(lua);
		LuaReflection::addClass<MouseButtonEvent>(lua);
		LuaReflection::addClass<MouseEnterEvent>(lua);
		LuaReflection::addClass<MouseLeaveEvent>(lua);
		LuaReflection::addClass<MouseFocusEvent>(lua);
		LuaReflection::addClass<MouseBlurEvent>(lua);
		//LuaReflection::addClass<KeyEvent>(lua);
		LuaReflection::addClass<CharEvent>(lua);

		//LuaReflection::addClass<LengthValue>(lua);

		luabridge::getGlobalNamespace(lua)
			.beginNamespace("fw")
				/*.beginClass<DomStyle>("DomStyle")
					.addProperty("cursor", &DomStyle::getCursor, &DomStyle::setCursor)
					.addProperty("flexDirection", &DomStyle::getFlexDirection, &DomStyle::setFlexDirection)
					.addProperty("justifyContent", &DomStyle::getJustifyContent, &DomStyle::setJustifyContent)
					.addProperty("flexAlignItems", &DomStyle::getFlexAlignItems, &DomStyle::setFlexAlignItems)
					.addProperty("flexAlignSelf", &DomStyle::getFlexAlignSelf, &DomStyle::setFlexAlignSelf)
					.addProperty("flexAlignContent", &DomStyle::getFlexAlignContent, &DomStyle::setFlexAlignContent)
					.addProperty("layoutDirection", &DomStyle::getLayoutDirection, &DomStyle::setLayoutDirection)
					.addProperty("flexWrap", &DomStyle::getFlexWrap, &DomStyle::setFlexWrap)
					.addProperty("flexGrow", &DomStyle::getFlexGrow, &DomStyle::setFlexGrow)
					.addProperty("flexShrink", &DomStyle::getFlexShrink, &DomStyle::setFlexShrink)
					.addProperty("flexBasis", &DomStyle::getFlexBasis, &DomStyle::setFlexBasis)
					.addProperty("minWidth", &DomStyle::getMinWidth, &DomStyle::setMinWidth)
					.addProperty("maxWidth", &DomStyle::getMaxWidth, &DomStyle::setMaxWidth)
					.addProperty("minHeight", &DomStyle::getMinHeight, &DomStyle::setMinHeight)
					.addProperty("maxHeight", &DomStyle::getMaxHeight, &DomStyle::setMaxHeight)
					.addProperty("width", &DomStyle::getWidth, &DomStyle::setWidth)
					.addProperty("height", &DomStyle::getHeight, &DomStyle::setHeight)
					.addProperty("aspectRatio", &DomStyle::getAspectRatio, &DomStyle::setAspectRatio)
					.addProperty("position", &DomStyle::getPosition, &DomStyle::setPosition)
					.addProperty("padding", &DomStyle::getPadding, &DomStyle::setPadding)
					.addProperty("margin", &DomStyle::getMargin, &DomStyle::setMargin)
					.addProperty("border", &DomStyle::getBorder, &DomStyle::setBorder)
					.addProperty("overflow", &DomStyle::getOverflow, &DomStyle::setOverflow)
					.addProperty("flexPositionType", &DomStyle::getFlexPositionType, &DomStyle::setFlexPositionType)
			
					.addProperty("color", &DomStyle::getColor, &DomStyle::setColor)
					.addProperty("backgroundColor", &DomStyle::getBackgroundColor, &DomStyle::setBackgroundColor)
					.addProperty("nodeValue", &DomStyle::getNodeValue, &DomStyle::setNodeValue)
					.addProperty("className", &DomStyle::getClassName, &DomStyle::setClassName)
				.endClass()
				.beginClass<Document>("Document")
					.addFunction("createElement", &Document::createElement)
					.addFunction("createTextNode", &Document::createTextNode)
					.addFunction("appendChild", &Document::appendChild)
					.addFunction("removeChild", &Document::removeChild)
					.addFunction("getStyle", &Document::getStyle)
					.addFunction("getRootElement", &Document::getRootElement)
				.endClass()*/
			.endNamespace();
	}
}
