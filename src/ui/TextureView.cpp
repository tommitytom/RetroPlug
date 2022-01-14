#include "TextureView.h"

#include <nanovg.h>
#include "util/Image.h"

using namespace rp;

void TextureView::setImage(const Image& image) {
	NVGcontext* vg = getVg();

	if (_textureHandle == -1 || _textureSize != image.dimensions()) {
		destroyTexture();
		_textureHandle = nvgCreateImageRGBA(vg, image.w(), image.h(), NVG_IMAGE_NEAREST, (const unsigned char*)image.getData());
		_textureSize = image.dimensions();

		if (getSizingMode() == SizingMode::FitToContent) {
			setDimensions(_textureSize);
		}
	} else {
		nvgUpdateImage(vg, _textureHandle, (const unsigned char*)image.getData());
	}
}

void TextureView::onRender() {
	if (_textureHandle != -1) {
		NVGcontext* vg = getVg();
		Dimension<uint32> area = getDimensions();
		Rect<f32> areaf(0, 0, (f32)area.w, (f32)area.h);

		nvgBeginPath(vg);

		NVGpaint imgPaint = nvgImagePattern(vg, areaf.x, areaf.y, areaf.w, areaf.h, 0, _textureHandle, getAlpha());
		nvgRect(vg, areaf.x, areaf.y, areaf.w, areaf.h);
		nvgFillPaint(vg, imgPaint);
		nvgFill(vg);
	}
}

void TextureView::destroyTexture() {
	if (_textureHandle != -1) {
		nvgDeleteImage(getVg(), _textureHandle);
		_textureHandle = -1;
	}
}
