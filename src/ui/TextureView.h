#pragma once

#include "View.h"

namespace rp {
	class Image;

	class TextureView : public View {
	private:
		int _textureHandle = -1;
		DimensionT<uint32> _textureSize;

	public:
		TextureView() { setType<TextureView>(); }
		TextureView(Dimension dimensions) : View(dimensions) { setType<TextureView>(); }
		~TextureView() { destroyTexture(); }

		void setImage(const Image& image);

		void onRender(Canvas& canvas) override;

		void destroyTexture();
	};
}
