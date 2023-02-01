#pragma once

#include "ui/View.h"
#include "graphics/FontManager.h"

namespace fw {
	class LabelView : public View {
	private:
		Color4F _color = Color4F(1, 1, 1, 1);
		std::string _text;
		std::string _fontName = "Karla-Regular";
		f32 _fontSize = 12.0f;

		uint32 _alignment = TextAlignFlags::Left | TextAlignFlags::Baseline;

	public:
		LabelView() { setType<LabelView>(); }
		LabelView(Dimension dimensions, const Color4F& color = Color4F(1, 1, 1, 1)) : View(dimensions), _color(color) { setType<LabelView>(); }
		~LabelView() = default;

		void onInitialize() {
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
			if (isInitialized() && getSizingPolicy() != SizingPolicy::FitToParent) {
				DimensionF textArea = getFontManager().measureText(_text, _fontName, _fontSize);
				setDimensions((Dimension)textArea);
			}
		}

		void onRender(fw::Canvas& canvas) override {
			_alignment = TextAlignFlags::Left | TextAlignFlags::Top;

			canvas.setTextAlign(_alignment);
			canvas.setColor(_color);
			canvas.setFont(_fontName, _fontSize);
			canvas.text(0, 0, _text);
		}
	};

	using LabelViewPtr = std::shared_ptr<LabelView>;
}
