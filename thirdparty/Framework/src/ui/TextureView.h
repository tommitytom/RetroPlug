#pragma once

#include "ui/View.h"
#include "graphics/Texture.h"

namespace fw {
	class Image;

	class TextureView : public View {
	private:
		TextureHandle _texture;
		RectF _textureArea;

	public:
		TextureView() { setType<TextureView>(); }
		TextureView(Dimension dimensions) : View(dimensions) { setType<TextureView>(); }
		~TextureView() = default;

		void setImage(const Image& image);

		void setTexture(TextureHandle texture) {
			_texture = texture;
		}

		void clear() { _texture = TextureHandle(); }

		void onRender(Canvas& canvas) override;
	};
}
