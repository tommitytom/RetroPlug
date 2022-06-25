#include "TextureView.h"

#include <nanovg.h>
#include "util/Image.h"

using namespace rp;

void TextureView::setImage(const Image& image) {
	/*if (_textureHandle == -1 || _textureSize != image.dimensions()) {
		destroyTexture();
		_textureHandle = nvgCreateImageRGBA(vg, image.w(), image.h(), NVG_IMAGE_NEAREST, (const unsigned char*)image.getData());
		_textureSize = image.dimensions();

		if (getSizingPolicy() == SizingPolicy::FitToContent) {
			setDimensions((Dimension)_textureSize);
		}
	} else {
		nvgUpdateImage(vg, _textureHandle, (const unsigned char*)image.getData());
	}*/
}

void TextureView::onRender(Canvas& canvas) {
	/*if (_textureHandle != -1) {
		NVGcontext* vg = getVg();
		RectT<f32> areaf;
		areaf.dimensions = (DimensionF)getDimensions();

		nvgBeginPath(vg);

		NVGpaint imgPaint = nvgImagePattern(vg, areaf.x, areaf.y, areaf.w, areaf.h, 0, _textureHandle, getAlpha());
		nvgRect(vg, areaf.x, areaf.y, areaf.w, areaf.h);
		nvgFillPaint(vg, imgPaint);
		nvgFill(vg);
	}*/
}

void TextureView::destroyTexture() {
	if (_textureHandle != -1) {
		//nvgDeleteImage(getVg(), _textureHandle);
		_textureHandle = -1;
	}
}
