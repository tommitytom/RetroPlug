#pragma once

#include <tuple>

#include "graphics/Shader.h"

namespace rp::engine {
	std::pair<ShaderDesc, ShaderDesc> getDefaultShaders();
}
