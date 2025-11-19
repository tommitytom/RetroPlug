#include "UiReflect.h"

#include "foundation/TypeRegistry.h"
#include "ui/ViewLayout.h"

namespace orb {
	void UiReflect::reflect(orb::TypeRegistry& registry) {
		registry.addEnum<orb::FlexUnit>();

		registry.addEnum<orb::CursorType>();
		registry.addEnum<orb::FlexAlign>();
		registry.addEnum<orb::FlexDimension>();
		registry.addEnum<orb::LayoutDirection>();
		registry.addEnum<orb::FlexDisplay>();
		registry.addEnum<orb::FlexEdge>();
		registry.addEnum<orb::FlexDirection>();
		registry.addEnum<orb::FlexGutter>();
		registry.addEnum<orb::FlexJustify>();
		registry.addEnum<orb::FlexMeasureMode>();
		registry.addEnum<orb::FlexNodeType>();
		registry.addEnum<orb::FlexOverflow>();
		registry.addEnum<orb::FlexPositionType>();
		registry.addEnum<orb::FlexWrap>();

		registry.addType<orb::FlexValue>()
			.addProperty<&orb::FlexValue::getUnit, &orb::FlexValue::setUnit>("unit")
			.addProperty<&orb::FlexValue::getValue, &orb::FlexValue::setValue>("value")
			;

		registry.addType<orb::FlexRect>()
			.addField<&orb::FlexRect::top>("top")
			.addField<&orb::FlexRect::left>("left")
			.addField<&orb::FlexRect::bottom>("bottom")
			.addField<&orb::FlexRect::right>("right")
			;

		registry.addType<orb::FlexBorder>()
			.addField<&orb::FlexBorder::top>("top")
			.addField<&orb::FlexBorder::left>("left")
			.addField<&orb::FlexBorder::bottom>("bottom")
			.addField<&orb::FlexBorder::right>("right")
			;

		registry.addType<orb::ViewLayout>()
			.addProperty<&orb::ViewLayout::getFlexDirection, &orb::ViewLayout::setFlexDirection>("flexDirection")
			.addProperty<&orb::ViewLayout::getJustifyContent, &orb::ViewLayout::setJustifyContent>("justifyContent")
			.addProperty<&orb::ViewLayout::getFlexAlignItems, &orb::ViewLayout::setFlexAlignItems>("flexAlignItems")
			.addProperty<&orb::ViewLayout::getFlexAlignSelf, &orb::ViewLayout::setFlexAlignSelf>("flexAlignSelf")
			.addProperty<&orb::ViewLayout::getFlexAlignContent, &orb::ViewLayout::setFlexAlignContent>("flexAlignContent")
			.addProperty<&orb::ViewLayout::getLayoutDirection, &orb::ViewLayout::setLayoutDirection>("layoutDirection")
			.addProperty<&orb::ViewLayout::getFlexWrap, &orb::ViewLayout::setFlexWrap>("flexWrap")
			.addProperty<&orb::ViewLayout::getFlexGrow, &orb::ViewLayout::setFlexGrow>("flexGrow")
			.addProperty<&orb::ViewLayout::getFlexShrink, &orb::ViewLayout::setFlexShrink>("flexShrink")
			.addProperty<&orb::ViewLayout::getFlexBasis, &orb::ViewLayout::setFlexBasis>("flexBasis")
			.addProperty<&orb::ViewLayout::getMinWidth, &orb::ViewLayout::setMinWidth>("minWidth")
			.addProperty<&orb::ViewLayout::getMaxWidth, &orb::ViewLayout::setMaxWidth>("maxWidth")
			.addProperty<&orb::ViewLayout::getMinHeight, &orb::ViewLayout::setMinHeight>("minHeight")
			.addProperty<&orb::ViewLayout::getMaxHeight, &orb::ViewLayout::setMaxHeight>("maxHeight")
			.addProperty<&orb::ViewLayout::getWidth, &orb::ViewLayout::setWidth>("width")
			.addProperty<&orb::ViewLayout::getHeight, &orb::ViewLayout::setHeight>("height")
			.addProperty<&orb::ViewLayout::getAspectRatio, &orb::ViewLayout::setAspectRatio>("aspectRatio")
			.addProperty<&orb::ViewLayout::getPosition, &orb::ViewLayout::setPosition>("position")
			.addProperty<&orb::ViewLayout::getPadding, &orb::ViewLayout::setPadding>("padding")
			.addProperty<&orb::ViewLayout::getMargin, &orb::ViewLayout::setMargin>("margin")
			.addProperty<&orb::ViewLayout::getBorder, &orb::ViewLayout::setBorder>("border")
			.addProperty<&orb::ViewLayout::getOverflow, &orb::ViewLayout::setOverflow>("overflow")
			;
	}
}
