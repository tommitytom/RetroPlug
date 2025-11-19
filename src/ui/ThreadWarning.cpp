#include "ThreadWarning.h"

namespace rp {
	ThreadWarning::ThreadWarning() {
		setName("Audio Thread Warning");

		setColor(orb::Color4(207, 39, 39, 240));
		setBorderColor(orb::Color4F(1, 0, 0, 1));

		orb::ViewLayout& layout = getLayout();
		layout.setFlexPositionType(orb::FlexPositionType::Absolute);
		layout.setFlexAlignItems(orb::FlexAlign::Center);
		layout.setJustifyContent(orb::FlexJustify::Center);
		layout.setHeight(orb::FlexValue(orb::FlexUnit::Percent, 10));
		layout.setWidth(orb::FlexValue(orb::FlexUnit::Percent, 90));
		layout.setPositionEdge(orb::FlexEdge::Left, orb::FlexValue(orb::FlexUnit::Percent, 5));
		layout.setPositionEdge(orb::FlexEdge::Bottom, orb::FlexValue(orb::FlexUnit::Percent, 5));
	}

	void ThreadWarning::onInitialize() {
		auto text = addChild<orb::LabelView>("Audio Thread Warning Text");
		text->setText("Audio thread inactive - check settings");
		text->setFont("PlatNomor", 7);
	}

	void ThreadWarning::setTextScale(f32 scale) {
		findChild<orb::LabelView>()->setFont("PlatNomor", 7 * scale);
	}
}