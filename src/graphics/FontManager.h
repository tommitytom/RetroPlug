#pragma once

#include "foundation/ResourceManager.h"
#include "graphics/Font.h"

namespace rp::engine {
	class FontManager {
	private:
		ResourceManager& _resourceManager;

	public:
		FontManager(ResourceManager& resourceManager): _resourceManager(resourceManager) {}
		~FontManager() = default;

		FontHandle loadFont(std::string_view uri, f32 size) {
			return _resourceManager.load<Font>(fmt::format("{}/{}", uri, size));
		}

		DimensionF measureText(std::string_view text, std::string_view fontName, f32 fontSize);
	};
}
