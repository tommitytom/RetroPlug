#pragma once

#include "ui/View.h"
#include "graphics/FontManager.h"

namespace fw {
	class LabelView : public View {
		RegisterObject()
	private:
		Color4F _color = Color4F(1, 1, 1, 1);
		std::string _text;
		std::string _fontName = "Karla-Regular";
		f32 _fontSize = 12.0f;

		uint32 _alignment = TextAlignFlags::Left | TextAlignFlags::Top;

	public:
		LabelView() {}
		LabelView(Dimension dimensions, const Color4F& color = Color4F(1, 1, 1, 1)) : View(dimensions), _color(color) {}
		~LabelView() = default;

		void onInitialize() override {
			//setSizingPolicy(SizingPolicy::FitToContent);
		}

		void setColor(const Color4F& color) {
			_color = color;
		}

		const Color4F getColor() const {
			return _color;
		}

		void setText(std::string_view text) {
			_text = std::string(text);
			updateArea();
		}

		const std::string& getText() const {
			return _text;
		}

		void setFont(std::string_view name, f32 size) {
			_fontName = std::string(name);
			_fontSize = size;
			updateArea();
		}

		void setFontFace(std::string_view name) {
			_fontName = std::string(name);
			updateArea();
		}

		const std::string& getFontFace() const {
			return _fontName;
		}

		void setFontSize(f32 size) {
			_fontSize = size;
			updateArea();
		}

		f32 getFontSize() const {
			return _fontSize;
		}

		void updateArea() {
			/*if (isInitialized() && getSizingPolicy() != SizingPolicy::FitToParent) {
				DimensionF textArea = getFontManager().measureText(_text, _fontName, _fontSize);
				setDimensions((Dimension)textArea);
			}*/
		}

		void setTextAlignment(uint32 flags) {
			_alignment = flags;
		}

		void onRender(fw::Canvas& canvas) override {
			canvas.setFont(_fontName, _fontSize);
			_alignment = TextAlignFlags::Middle | TextAlignFlags::Left;
			
			DimensionF dim = getDimensionsF();
			DimensionF textSize = canvas.measureText(_text);
			RectF textArea({ 0, 0 }, textSize);

			f32 xDiff = dim.w - textSize.w;
			f32 yDiff = dim.h - textSize.h;

			if (_alignment & TextAlignFlags::Center) {	
				textArea.x += xDiff / 2;
			} else if (_alignment & TextAlignFlags::Right) {
				textArea.x = textSize.w - xDiff;
			}

			if (_alignment & TextAlignFlags::Middle) {
				textArea.y += yDiff / 2;
			} else if (_alignment & TextAlignFlags::Bottom) {
				textArea.y = textSize.h - yDiff;
			}

			uint32 lastAlign = canvas.getTextAlign();
			canvas.setTextAlign(TextAlignFlags::Top | TextAlignFlags::Left);

			canvas.text(textArea.position, _text, _color);

			canvas.setTextAlign(lastAlign);
		}
	};

	using LabelViewPtr = std::shared_ptr<LabelView>;
}

REFL_AUTO(
	type(fw::LabelView, bases<fw::View>),
	func(setText, property("text")), func(getText, property("text"))
)
