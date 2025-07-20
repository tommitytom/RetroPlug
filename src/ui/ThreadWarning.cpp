#include "ThreadWarning.h"

namespace rp {
	ThreadWarning::ThreadWarning() {
		setName("Audio Thread Warning");

		setColor(fw::Color4(207, 39, 39, 240));
		setBorderColor(fw::Color4F(1, 0, 0, 1));

		fw::ViewLayout& layout = getLayout();
		layout.setFlexPositionType(fw::FlexPositionType::Absolute);
		layout.setFlexAlignItems(fw::FlexAlign::Center);
		layout.setJustifyContent(fw::FlexJustify::Center);
		layout.setHeight(fw::FlexValue::FlexValue(fw::FlexUnit::Percent, 10));
		layout.setWidth(fw::FlexValue::FlexValue(fw::FlexUnit::Percent, 90));
		layout.setPositionEdge(fw::FlexEdge::Left, fw::FlexValue::FlexValue(fw::FlexUnit::Percent, 5));
		layout.setPositionEdge(fw::FlexEdge::Bottom, fw::FlexValue::FlexValue(fw::FlexUnit::Percent, 5));

		auto text = addChild<fw::LabelView>("Audio Thread Warning Text");
		text->setText("Audio thread inactive - check settings");
		text->setFont("PlatNomor", 7);
	}
	
	void ThreadWarning::setTextScale(f32 scale) {
		findChild<fw::LabelView>()->setFont("PlatNomor", 7 * scale);
	}
}