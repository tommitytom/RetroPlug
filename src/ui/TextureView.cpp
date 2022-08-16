#include "TextureView.h"

#include "util/Image.h"
#include "foundation/ResourceManager.h"

using namespace rp;

void TextureView::setImage(const Image& image) {
	size_t dataSize = image.getBuffer().size() * 4;
	std::vector<uint8> data(dataSize);
	memcpy(data.data(), image.getData(), dataSize);

	if (!_texture.isValid() || (Dimension)_textureArea.dimensions != image.dimensions()) {
		_texture = getResourceManager().create<Texture>(TextureDesc {
			.dimensions = (Dimension)image.dimensions(),
			.depth = 4,
			.data = std::move(data)
		});

		_textureArea = { 0.0f, 0.0f, (f32)image.dimensions().w, (f32)image.dimensions().h };

		if (getSizingPolicy() == SizingPolicy::FitToContent) {
			setDimensions((Dimension)image.dimensions());
		}
	} else {
		getResourceManager().update(_texture, TextureDesc {
			.dimensions = (Dimension)image.dimensions(),
			.depth = 4,
			.data = std::move(data)
		});
	}
}

void TextureView::onRender(Canvas& canvas) {
	if (_texture.isValid()) {
		canvas.texture(_texture, _textureArea, Color4F(1, 1, 1, 1));
	} else {
		canvas.fillRect(_textureArea, Color4F(1, 1, 1, 1));
	}
}
