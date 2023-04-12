#include "FontManager.h"

#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>

#include "graphics/ftgl/FtglFont.h"

using namespace fw;


FontFaceHandle FontManager::loadFont(std::string_view fontUri, f32 size) {
	std::string fontFaceUri = fmt::format("{}/{}", fontUri, size);

	FontFaceHandle fontFace = _resourceManager->get<FontFace>(fontFaceUri);
	if (fontFace.isValid()) {
		return fontFace;
	}

	return _resourceManager->create<FontFace>(fontFaceUri, FontFaceDesc {
		.font = std::string(fontUri),
		.size = size
	});
}

DimensionF FontManager::measureText(std::string_view text, std::string_view fontName, f32 fontSize) {
	FontFaceHandle handle = loadFont(fontName, fontSize);
	FtglFontFace& font = handle.getResourceAs<FtglFontFace>();
	ftgl::texture_font_t* textureFont = font.getTextureFont();

	DimensionF ret(0, 0);

	for (size_t i = 0; i < text.size(); ++i) {
		ftgl::texture_glyph_t* glyph = ftgl::texture_font_get_glyph(textureFont, text.data() + i);

		if ((f32)glyph->height > ret.h) {
			ret.h = (f32)glyph->height;
		}

		if (i > 0) {
			ret.w += ftgl::texture_glyph_get_kerning(glyph, text.data() + i - 1);
		}

		ret.w += glyph->advance_x;
	}

	return ret;
}
