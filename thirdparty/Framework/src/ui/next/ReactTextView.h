#pragma once

#include "ui/next/ReactElementView.h"

namespace fw {
	class ReactTextView : public ReactElementView {
		RegisterObject()

	private:
		std::string _nodeValue;

		struct FontSettings {
			Color4F color = Color4F::black;
			std::string_view family = "Karla-Regular";
			f32 size = 12.0f;
			TextAlignType textAlign = TextAlignType::Left;
		};

	public:
		ReactTextView() {}
		ReactTextView(const std::string& text): _nodeValue(text) {}

		void onInitialize() override;

		void onRender(fw::Canvas& canvas) override;

		const std::string& getNodeValue() const {
			return _nodeValue;
		}

		void setNodeValue(const std::string& value) {
			_nodeValue = value;
		}

	private:
		void getFontSettings(FontSettings& settings);

		static YGSize measureText(YGNodeRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode);
	};
}
