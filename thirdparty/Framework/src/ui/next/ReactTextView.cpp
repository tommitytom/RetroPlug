#include "ReactTextView.h"

namespace fw {
	void ReactTextView::onInitialize() {
		getLayout().setMeasureFunc(measureText);
		ReactElementView::onInitialize();
	}
	
	void ReactTextView::onRender(fw::Canvas& canvas) {
		FontSettings settings;
		getFontSettings(settings);
		
		const uint32 lastAlign = canvas.getTextAlign();
		uint32 align = TextAlignFlags::Top;		

		if (settings.textAlign == TextAlignType::Center) {
			align |= TextAlignFlags::Center;
		} else if (settings.textAlign == TextAlignType::Right) {
			align |= TextAlignFlags::Right;
		}

		canvas.setTextAlign(align);
		canvas.setFont(settings.family, settings.size);
		canvas.text(getDimensionsF(), _nodeValue, settings.color);

		canvas.setTextAlign(lastAlign);
	}
	
	void ReactTextView::getFontSettings(FontSettings& settings) {
		const styles::Color* colorProp = findStyleProperty<styles::Color>();
		const styles::FontFamily* familyProp = findStyleProperty<styles::FontFamily>();
		const styles::FontSize* sizeProp = findStyleProperty<styles::FontSize>();
		const styles::TextAlign* textAlignProp = findStyleProperty<styles::TextAlign>();
		//const styles::FontWeight* weightProp = findStyleProperty<styles::FontWeight>();

		if (colorProp) { settings.color = colorProp->value; }
		if (familyProp) { settings.family = familyProp->value.familyName; }
		if (sizeProp) { settings.size = sizeProp->value.value; }
		if (textAlignProp) { settings.textAlign = textAlignProp->value; }
	}
	
	YGSize ReactTextView::measureText(YGNodeRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
		void* ctx = YGNodeGetContext(node);
		ReactTextView* view = (ReactTextView*)ctx;

		FontSettings settings;
		view->getFontSettings(settings);

		DimensionF textArea = view->getFontManager().measureText(view->getNodeValue(), settings.family, settings.size);

		return YGSize{
			.width = textArea.w,
			.height = textArea.h
		};
	}
}
