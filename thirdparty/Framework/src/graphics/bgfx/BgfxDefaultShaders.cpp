#include "BgfxDefaultShaders.h"

#include <bgfx/bgfx.h>

#include "shaders/fs_tex_gl.h"
#include "shaders/vs_tex_gl.h"
#include "shaders/fs_tex_d3d9.h"
#include "shaders/vs_tex_d3d9.h"
#include "shaders/fs_tex_d3d11.h"
#include "shaders/vs_tex_d3d11.h"
#include "shaders/fs_tex_spirv.h"
#include "shaders/vs_tex_spirv.h"
#include "shaders/fs_tex_metal.h"
#include "shaders/vs_tex_metal.h"

using namespace fw;
using namespace fw::engine;

std::pair<ShaderDesc, ShaderDesc> engine::getDefaultShaders() {
	const uint8* vert = nullptr; uint32 vertSize = 0;
	const uint8* frag = nullptr; uint32 fragSize = 0;

	switch (bgfx::getRendererType()) {
	case bgfx::RendererType::Direct3D9:
		vert = vs_tex_d3d9; vertSize = sizeof(vs_tex_d3d9);
		frag = fs_tex_d3d9; fragSize = sizeof(fs_tex_d3d9);
		break;
	case bgfx::RendererType::Direct3D11:
	case bgfx::RendererType::Direct3D12:
		vert = vs_tex_d3d11; vertSize = sizeof(vs_tex_d3d11);
		frag = fs_tex_d3d11; fragSize = sizeof(fs_tex_d3d11);
		break;
	case bgfx::RendererType::OpenGL:
	case bgfx::RendererType::OpenGLES:
		vert = vs_tex_gl; vertSize = sizeof(vs_tex_gl);
		frag = fs_tex_gl; fragSize = sizeof(fs_tex_gl);
		break;
	case bgfx::RendererType::Vulkan:
	case bgfx::RendererType::WebGPU:
		vert = vs_tex_spirv; vertSize = sizeof(vs_tex_spirv);
		frag = fs_tex_spirv; fragSize = sizeof(fs_tex_spirv);
		break;
	case bgfx::RendererType::Metal:
		vert = vs_tex_metal; vertSize = sizeof(vs_tex_metal);
		frag = fs_tex_metal; fragSize = sizeof(fs_tex_metal);
		break;
	}

	assert(vert && vertSize);
	assert(frag && fragSize);

	return { ShaderDesc{ vert, vertSize }, ShaderDesc{ frag, fragSize } };
}
