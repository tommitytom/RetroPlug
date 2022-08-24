#pragma once

#include "foundation/Math.h"
#include "ui/LabelView.h"
#include "ui/PanelView.h"

namespace fw {
	class UiScaling : public View {
	private:
		PanelViewPtr _panel1;
		PanelViewPtr _panel2;
		LabelViewPtr _label;

	public:
		UiScaling() : View({ 1024, 768 }) { setType<UiScaling>(); }
		~UiScaling() = default;

		void onInitialize() override {
			_label = addChild<LabelView>("Label 1");
			_label->setText("Shiiiiiiit");
			_label->setPosition(10, 10);

			_panel1 = addChild<PanelView>("Panel 1");
			_panel2 = addChild<PanelView>("Panel 2");

			_panel1->setArea({ 100, 100, 100, 100 });
			_panel2->setArea({ 250, 100, 100, 100 });

			PanelViewPtr inner1 = _panel1->addChild<PanelView>("Panel 1 Inner");
			PanelViewPtr inner2 = _panel2->addChild<PanelView>("Panel 2 Inner");

			inner1->setArea({ 25, 25, 50, 50 });
			inner1->setColor({ 1, 0, 0, 1 });
			inner2->setArea({ 25, 25, 50, 50 });
			inner2->setColor({ 1, 0, 0, 1 });

			_panel2->setScale(2.0f);
		}

		void onUpdate(f32 delta) override {
			_label->setPosition(50, 50);
			_label->setFont("PlatNomor.ttf", 40);
		}

		void onRender(Canvas& canvas) override {
			canvas.fillRect((Rect)getDimensions(), Color4F(0, 0, 0, 1));
		}
	};
}
