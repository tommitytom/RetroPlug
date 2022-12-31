#pragma once

#include <tuple>

#include "graphics/Shader.h"

namespace fw::engine {
	std::pair<ShaderDesc, ShaderDesc> getDefaultGlShaders();
}
