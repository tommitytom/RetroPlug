#include "FontUtil.h"

#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>

#include "graphics/ftgl/FtglFont.h"

namespace fw {
	DimensionF FontUtil::measureText(std::string_view text, FontFaceHandle handle) {
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
}
