#pragma once

#include "foundation/ResourceManager.h"
#include "graphics/Font.h"

namespace fw {
	class FontManager {
	private:
		ResourceManager& _resourceManager;

	public:
		FontManager(ResourceManager& resourceManager): _resourceManager(resourceManager) {}
		~FontManager() = default;

		FontFaceHandle loadFont(std::string_view fontUri, f32 size);

		DimensionF measureText(std::string_view text, std::string_view fontName, f32 fontSize);
	};
}
