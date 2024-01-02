#pragma once

#include "ui/View.h"

namespace fw {
	class PanelView : public View {
		RegisterObject()
	private:
		Color4F _color = Color4F::clear;

	public:	
		PanelView() {}
		PanelView(Dimension dimensions, const Color4F& color = Color4F(1, 1, 1, 1)) : View(dimensions), _color(color) {}
		~PanelView() = default;

		void setColor(const Color4F& color) {
			_color = color;
		}

		const Color4F& getColor() const {
			return _color;
		}

		void onRender(fw::Canvas& canvas) override {
			canvas.fillRect((Rect)getDimensions(), _color);
		}
	};

	using PanelViewPtr = std::shared_ptr<PanelView>;
}

#include "foundation/MathMeta.h"

REFL_AUTO(
	type(fw::PanelView, bases<fw::View>),
	func(getColor, property("color")), func(setColor, property("color"))
)
