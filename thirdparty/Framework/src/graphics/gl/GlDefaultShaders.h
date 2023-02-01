#pragma once

#include <tuple>

#include "graphics/Shader.h"

namespace fw {
	std::pair<ShaderDesc, ShaderDesc> getDefaultGlShaders();
}
