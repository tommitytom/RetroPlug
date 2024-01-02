#pragma once

#include "ui/View.h"

namespace fw {
	struct ButtonClickEvent {};

	class ButtonView : public View {
		RegisterObject()
			
	private:
		std::string _text = "Button";
		bool _mouseOver = false;
		bool _mouseDown = false;

	public:
		ButtonView() {
			setFocusPolicy(FocusPolicy::Click);
		}
		~ButtonView() = default;

		void setText(const std::string& text) {
			_text = text;
		}

		const std::string& getText() const {
			return _text;
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			if (ev.button == MouseButton::Left) {
				_mouseDown = ev.down;

				if (ev.down) {
					emit(ButtonClickEvent());
				}
			}

			return true;
		}

		void onMouseEnter(Point pos) override {
			_mouseOver = true;
		}

		void onMouseLeave() override {
			_mouseOver = false;
		}

		void onRender(fw::Canvas& canvas) {
			Color4F bgColor = Color4F::darkGrey;
			if (_mouseOver) { bgColor = Color4F::lightGrey; }
			if (_mouseDown) { bgColor = Color4F(0.4f, 0.4f, 0.4f, 1.0f); }

			canvas
				.fillRect(getDimensionsF(), bgColor)
				.strokeRect(getDimensionsF(), Color4F::black)
				.setTextAlign(TextAlignFlags::Middle | TextAlignFlags::Center)
				.text(getDimensionsF(), _text, Color4F::white);
		}
	};
}

REFL_AUTO(
	type(fw::ButtonView, bases<fw::View>),
	func(getText, property("text")), func(setText, property("text"))
)
