#pragma once

#include "foundation/ResourceManager.h"
#include "foundation/ResourceProvider.h"
#include "graphics/Font.h"
#include "graphics/Texture.h"

namespace ftgl {
	struct texture_atlas_t;
	struct texture_font_t;
}

namespace fw::engine {
	class FtglFontFace final : public FontFace {
	private:
		TextureHandle _texture;
		ftgl::texture_atlas_t* _atlas = nullptr;
		ftgl::texture_font_t* _font = nullptr;

	public:
		FtglFontFace(TextureHandle texture, ftgl::texture_atlas_t* atlas, ftgl::texture_font_t* font) : _texture(texture), _atlas(atlas), _font(font) {}
		~FtglFontFace();

		TextureHandle getTexture() {
			return _texture;
		}

		ftgl::texture_atlas_t* getAtlas() {
			return _atlas;
		}

		ftgl::texture_font_t* getTextureFont() {
			return _font;
		}
	};

	class FtglFontFaceProvider : public TypedResourceProvider<FontFace> {
	private:
		ResourceManager& _resourceManager;
		std::shared_ptr<FontFace> _default;

	public:
		FtglFontFaceProvider(ResourceManager& resourceManager);
		~FtglFontFaceProvider() = default;

		std::shared_ptr<Resource> load(std::string_view uri) override;

		std::shared_ptr<Resource> create(const FontFaceDesc& desc, std::vector<std::string>& deps) override;

		bool update(FontFace& fontFace, const FontFaceDesc& desc) override;
	};
}
