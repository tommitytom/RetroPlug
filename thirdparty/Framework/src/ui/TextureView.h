#pragma once

#include "ui/View.h"
#include "graphics/Texture.h"

namespace fw {
	class Image;

	class TextureView : public View {
	private:
		std::string _uri;
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

		void setUri(const std::string& uri);

		std::string_view getUri() const {
			if (_texture.isValid()) {
				return _texture.getUri();
			}

			return "";
		}

		void clear() { _texture = TextureHandle(); }

		void onInitialize() override;

		void onRender(fw::Canvas& canvas) override;
	};
}
