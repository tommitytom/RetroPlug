#pragma once

#include <charconv>

#include "ui/Property.h"
#include "ui/View.h"

namespace fw {
	template <typename T>
	class TextEditBaseView : public TypedPropertyEditor<T> {
	private:
		std::string _text;
		std::string _placeholder;
		uint32 _cursorOffset = 0;
		bool _showCursor = true;

		std::string _fontName = "Karla-Regular";
		f32 _fontSize = 12.0f;

		std::function<bool(const std::string&)> _validator;

	public:
		std::function<void(const std::string&)> TextChangeEvent;
		
		TextEditBaseView() {
			this->setFocusPolicy(FocusPolicy::Click);
		}

		void setValidator(std::function<bool(const std::string&)>&& validator) {
			_validator = std::move(validator);
		}

		void setPlaceholder(const std::string& text) {
			_placeholder = text;
		}

		void setFont(std::string_view name, f32 size) {
			_fontName = std::string(name);
			_fontSize = size;
		}

		bool setText(const std::string& text) {	
			if (!_validator || _validator(text)) {
				_text = text;
				onTextUpdated(_text);
				return true;
			}

			return false;
		}

		std::string getText() const {
			return _text;
		}
		
		virtual void onTextUpdated(const std::string& value) {}

		bool onKey(const KeyEvent& ev) override {
			if (ev.action == KeyAction::Press || ev.action == KeyAction::Repeat) {
				switch (ev.key) {
				case VirtualKey::Backspace:
					if (_cursorOffset > 0) {
						_text.erase(_cursorOffset - 1, 1);
						setText(_text);
						_cursorOffset--;
					}
					break;
				case VirtualKey::Delete:
					if (_cursorOffset < _text.size()) {
						_text.erase(_cursorOffset, 1);
						setText(_text);
					}
					break;
				case VirtualKey::LeftArrow:
					if (_cursorOffset > 0) {
						_cursorOffset--;
					}
					break;

				case VirtualKey::RightArrow:
					if (_cursorOffset < _text.size()) {
						_cursorOffset++;
					}
					break;
				}
			}

			return true;
		}

		bool onChar(const CharEvent& ev) override {
			std::string text = _text;
			text.insert(_cursorOffset, 1, (char)ev.keyCode);
			
			if (setText(text)) {
				_cursorOffset++;
			}
			
			return true;
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			if (ev.button == MouseButton::Left && ev.down) {
				//_view->hitTest();
			}

			return true;
		}

		void onRender(fw::Canvas& canvas) override {
			std::string_view text = !_text.empty() ? _text : _placeholder;

			canvas.setFont(_fontName, _fontSize);
			f32 cursorPos = canvas.measureText(_text.substr(0, _cursorOffset)).w;
			canvas.strokeRect(this->getDimensionsF(), Color4F::black);
			canvas.setTextAlign(TextAlignFlags::Left | TextAlignFlags::Middle);
			canvas.text(this->getDimensionsF(), text, Color4F::white);
			canvas.line(PointF{ cursorPos , 0 }, PointF{ cursorPos, this->getDimensionsF().h }, Color4F::white);
		}
	};

	class TextEditView : public TextEditBaseView<std::string> {
	public:
		TextEditView() {
			setType<TextEditView>();
		}

		void setValue(const std::string& value) override {
			setText(value);
		}

		std::string getValue() const override {
			return getText();
		}
	};

	class FlexValueEditView : public TextEditBaseView<FlexValue> {
	private:
		FlexValue _value;

	public:
		std::function<void(const FlexValue&)> ValueChangeEvent;
		
		FlexValueEditView() {
			setType<FlexValueEditView>();
			setPlaceholder("auto");
			
			setValidator([](const std::string& text) -> bool {
				if (text.empty()) {
					return true;
				}

				auto foundInvalid = std::find_if(text.begin(), text.end(), [](char c) {
					return !(std::isdigit(c) || c == '.' || c == '-' || c == '+' || c == '%');
				});

				if (foundInvalid != text.end()) {
					return false;
				}

				FlexUnit unit = FlexUnit::Point;
				std::string_view number = text;

				if (number.back() == '%') {
					unit = FlexUnit::Percent;
					number = std::string_view(text.data(), text.size() - 1);
				}

				f32 v;
				std::from_chars_result res = std::from_chars(number.data(), number.data() + number.size(), v);
				if (res.ec != std::errc()) {
					spdlog::error("Failed to convert '{}' to float", number);
					return false;
				}

				return true;
			});
		}

		void onTextUpdated(const std::string& value) override {
			if (value.empty()) {
				_value = FlexUnit::Auto;
				return;
			}

			FlexUnit unit = FlexUnit::Point;
			std::string_view number = value;
			
			if (value.back() == '%') {
				unit = FlexUnit::Percent;
				number = std::string_view(value.data(), value.size() - 1);
			}

			f32 v;
			std::from_chars_result res = std::from_chars(number.data(), number.data() + number.size(), v);
			
			if (res.ec != std::errc()) {
				spdlog::error("Failed to convert '{}' to float", number);
				return;
			}

			if (unit == FlexUnit::Percent) {
				v /= 100.0f;
			}

			_value = FlexValue(unit, v);
			
			if (ValueChangeEvent) {
				ValueChangeEvent(_value);
			}
		}

		void setValue(const FlexValue& value) {
			_value = value;
			
			switch (value.getUnit()) {
			case FlexUnit::Point:
				setText(fmt::format("{}", value.getValue()));
				break;
			case FlexUnit::Percent:
				setText(fmt::format("{}%", value.getValue() * 100.0f));
				break;
			default:
				setText("");
			}
		}

		FlexValue getValue() const {
			return _value;
		}

		FlexValue& getValue() {
			return _value;
		}
	};

	using TextEditViewPtr = std::shared_ptr<TextEditView>;
	using FlexValueEditViewPtr = std::shared_ptr<FlexValueEditView>;
}

REFL_AUTO(
	type(fw::TextEditView)
)

REFL_AUTO(
	type(fw::FlexValueEditView)
)
