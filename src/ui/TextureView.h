#pragma once

#include "View.h"
#include "graphics/Texture.h"

namespace rp {
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

		void clear() { _texture = TextureHandle(); }

		void onRender(Canvas& canvas) override;
	};
}
