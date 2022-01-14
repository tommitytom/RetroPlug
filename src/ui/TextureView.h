#pragma once

#include "View.h"

namespace rp {
	class Image;

	class TextureView : public View {
	private:
		int _textureHandle = -1;
		Dimension<uint32> _textureSize;

	public:
		TextureView() { setType<TextureView>(); }
		TextureView(Dimension<uint32> dimensions) : View(dimensions) { setType<TextureView>(); }
		~TextureView() { destroyTexture(); }

		void setImage(const Image& image);

		void onRender() override;

		void destroyTexture();
	};
}
