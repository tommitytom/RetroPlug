#pragma once

#include "ui/View.h"

namespace fw {
	class PanelView : public View {
		FwRegisterObject()
	private:
		Color4F _color = Color4F::clear;
		Color4F _borderColor = Color4F::clear;
		uint32 _borderWidth = 1;

	public:	
		PanelView() {}
		PanelView(Dimension dimensions, const Color4F& color = Color4F::clear, const Color4F& borderColor = Color4F::clear, uint32 borderWidth = 1) :
			View(dimensions), 
			_color(color),
			_borderColor(borderColor),
			_borderWidth(borderWidth)
		{}
		~PanelView() = default;

		void setBorderColor(const Color4F& color) {
			_borderColor = color;
		}

		const Color4F& getBorderColor() const {
			return _borderColor;
		}

		void setColor(const Color4F& color) {
			_color = color;
		}

		const Color4F& getColor() const {
			return _color;
		}

		void onRender(fw::Canvas& canvas) override {
			if (_color.a > 0.0f) {
				canvas.fillRect((Rect)getDimensions(), _color);
			}

			if (_borderColor.a > 0.0f) {
				canvas.strokeRect((Rect)getDimensions(), _borderColor);
			}
		}
	};

	using PanelViewPtr = std::shared_ptr<PanelView>;
}
