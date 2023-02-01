#include "GlDefaultShaders.h"

#include "graphics/gl/shaders/fs_tex.h"
#include "graphics/gl/shaders/vs_tex.h"

namespace fw {
	std::pair<ShaderDesc, ShaderDesc> getDefaultGlShaders() {
		return {
			ShaderDesc{
				.data = vs_tex,
				.size = (uint32)vs_tex_len,
				.type = ShaderType::Vertex
			},
			ShaderDesc{
				.data = fs_tex,
				.size = (uint32)fs_tex_len,
				.type = ShaderType::Fragment
			}
		};
	}
}
