#pragma once

#include <string_view>
#include "graphics/Font.h"

namespace orb::FontUtil {
	DimensionF measureText(std::string_view text, FontFaceHandle handle);
}