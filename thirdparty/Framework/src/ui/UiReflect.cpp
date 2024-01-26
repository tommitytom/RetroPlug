#include "UiReflect.h"

#include "foundation/TypeRegistry.h"
#include "ui/ViewLayout.h"

namespace fw {
	void UiReflect::reflect(fw::TypeRegistry& registry) {
		registry.addEnum<fw::FlexUnit>();

		registry.addEnum<fw::CursorType>();
		registry.addEnum<fw::FlexAlign>();
		registry.addEnum<fw::FlexDimension>();
		registry.addEnum<fw::LayoutDirection>();
		registry.addEnum<fw::FlexDisplay>();
		registry.addEnum<fw::FlexEdge>();
		registry.addEnum<fw::FlexDirection>();
		registry.addEnum<fw::FlexGutter>();
		registry.addEnum<fw::FlexJustify>();
		registry.addEnum<fw::FlexMeasureMode>();
		registry.addEnum<fw::FlexNodeType>();
		registry.addEnum<fw::FlexOverflow>();
		registry.addEnum<fw::FlexPositionType>();
		registry.addEnum<fw::FlexWrap>();

		registry.addType<fw::FlexValue>()
			.addProperty<&fw::FlexValue::getUnit, &fw::FlexValue::setUnit>("unit")
			.addProperty<&fw::FlexValue::getValue, &fw::FlexValue::setValue>("value")
			;

		registry.addType<fw::FlexRect>()
			.addField<&fw::FlexRect::top>("top")
			.addField<&fw::FlexRect::left>("left")
			.addField<&fw::FlexRect::bottom>("bottom")
			.addField<&fw::FlexRect::right>("right")
			;

		registry.addType<fw::FlexBorder>()
			.addField<&fw::FlexBorder::top>("top")
			.addField<&fw::FlexBorder::left>("left")
			.addField<&fw::FlexBorder::bottom>("bottom")
			.addField<&fw::FlexBorder::right>("right")
			;

		registry.addType<fw::ViewLayout>()
			.addProperty<&fw::ViewLayout::getFlexDirection, &fw::ViewLayout::setFlexDirection>("flexDirection")
			.addProperty<&fw::ViewLayout::getJustifyContent, &fw::ViewLayout::setJustifyContent>("justifyContent")
			.addProperty<&fw::ViewLayout::getFlexAlignItems, &fw::ViewLayout::setFlexAlignItems>("flexAlignItems")
			.addProperty<&fw::ViewLayout::getFlexAlignSelf, &fw::ViewLayout::setFlexAlignSelf>("flexAlignSelf")
			.addProperty<&fw::ViewLayout::getFlexAlignContent, &fw::ViewLayout::setFlexAlignContent>("flexAlignContent")
			.addProperty<&fw::ViewLayout::getLayoutDirection, &fw::ViewLayout::setLayoutDirection>("layoutDirection")
			.addProperty<&fw::ViewLayout::getFlexWrap, &fw::ViewLayout::setFlexWrap>("flexWrap")
			.addProperty<&fw::ViewLayout::getFlexGrow, &fw::ViewLayout::setFlexGrow>("flexGrow")
			.addProperty<&fw::ViewLayout::getFlexShrink, &fw::ViewLayout::setFlexShrink>("flexShrink")
			.addProperty<&fw::ViewLayout::getFlexBasis, &fw::ViewLayout::setFlexBasis>("flexBasis")
			.addProperty<&fw::ViewLayout::getMinWidth, &fw::ViewLayout::setMinWidth>("minWidth")
			.addProperty<&fw::ViewLayout::getMaxWidth, &fw::ViewLayout::setMaxWidth>("maxWidth")
			.addProperty<&fw::ViewLayout::getMinHeight, &fw::ViewLayout::setMinHeight>("minHeight")
			.addProperty<&fw::ViewLayout::getMaxHeight, &fw::ViewLayout::setMaxHeight>("maxHeight")
			.addProperty<&fw::ViewLayout::getWidth, &fw::ViewLayout::setWidth>("width")
			.addProperty<&fw::ViewLayout::getHeight, &fw::ViewLayout::setHeight>("height")
			.addProperty<&fw::ViewLayout::getAspectRatio, &fw::ViewLayout::setAspectRatio>("aspectRatio")
			.addProperty<&fw::ViewLayout::getPosition, &fw::ViewLayout::setPosition>("position")
			.addProperty<&fw::ViewLayout::getPadding, &fw::ViewLayout::setPadding>("padding")
			.addProperty<&fw::ViewLayout::getMargin, &fw::ViewLayout::setMargin>("margin")
			.addProperty<&fw::ViewLayout::getBorder, &fw::ViewLayout::setBorder>("border")
			.addProperty<&fw::ViewLayout::getOverflow, &fw::ViewLayout::setOverflow>("overflow")
			;
	}
}
