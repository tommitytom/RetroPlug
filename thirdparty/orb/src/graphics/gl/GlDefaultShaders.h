#pragma once

#include <tuple>

#include "graphics/Shader.h"

namespace orb {
	std::pair<ShaderDesc, ShaderDesc> getDefaultGlShaders();
}
