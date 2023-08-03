#pragma once

#include <string>
#include "foundation/Math.h"
#include "graphics/Font.h"
#include "graphics/Texture.h"

namespace fw {
	struct TextComponent {
		std::string text;
	};

	struct TextureComponent {
		TextureHandle texture;
	};

	struct BackgroundColorStyle {
		Color4F color;
	};
	
	struct ColorStyle {
		Color4F color;
	};
	
	struct FontFaceStyle {
		FontFaceHandle handle;
	};
}
