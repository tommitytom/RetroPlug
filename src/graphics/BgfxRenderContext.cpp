#include "BgfxRenderContext.h"

#include <fstream>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include "shaders/fs_tex_gl.h"
#include "shaders/vs_tex_gl.h"
#include "shaders/fs_tex_d3d9.h"
#include "shaders/vs_tex_d3d9.h"
#include "shaders/fs_tex_d3d11.h"
#include "shaders/vs_tex_d3d11.h"
#include "shaders/fs_tex_spirv.h"
#include "shaders/vs_tex_spirv.h"

using namespace rp;
namespace fs = std::filesystem;

const bgfx::ViewId kClearView = 0;

bgfx::ShaderHandle loadShader(const uint8_t* data, size_t size, const char* name = nullptr) {
	const bgfx::Memory* mem = bgfx::makeRef(data, (uint32)size);
	bgfx::ShaderHandle handle = bgfx::createShader(mem);

	if (name) {
		bgfx::setName(handle, name);
	}

	return handle;
}

ShaderProgram rp::loadShaders() {
	const uint8* vert = nullptr; size_t vertSize = 0;
	const uint8* frag = nullptr; size_t fragSize = 0;

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
		vert = vs_tex_spirv; vertSize = sizeof(vs_tex_spirv);
		frag = fs_tex_spirv; fragSize = sizeof(fs_tex_spirv);
		break;
	}

	assert(vert && vertSize);
	assert(frag && fragSize);

	return ShaderProgram{
		loadShader(vert, vertSize, "Canvas Vertex Shader"),
		loadShader(frag, fragSize, "Canvas Pixel Shader")
	};
}

BgfxRenderContext::BgfxRenderContext(void* nativeWindowHandle, Dimension res) {
	bgfx::PlatformData pd;
	pd.nwh = nativeWindowHandle;
	bgfx::setPlatformData(pd);

	bgfx::Init bgfxInit;
	bgfxInit.type = bgfx::RendererType::OpenGL;
	bgfxInit.resolution.width = res.w;
	bgfxInit.resolution.height = res.h;
	bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
	bgfxInit.platformData.nwh = nativeWindowHandle;

	bgfx::init(bgfxInit);

	ShaderProgram prog = loadShaders();
	_prog = bgfx::createProgram(prog.vert, prog.frag, true);

	_textureUniform = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
	_scaleUniform = bgfx::createUniform("scale", bgfx::UniformType::Vec4);

	bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x000000FF, 0.0f);
	bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
	bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
}

BgfxRenderContext::~BgfxRenderContext() {
	bgfx::shutdown();
}

void BgfxRenderContext::beginFrame() {
	
}

void BgfxRenderContext::renderCanvas(engine::Canvas& canvas) {
	const engine::CanvasGeometry& geom = canvas.getGeometry();
	
	if (geom.vertices.size()) {
		const bgfx::Memory* verts = bgfx::copy(geom.vertices.data(), (uint32)geom.vertices.size() * sizeof(engine::CanvasVertex));
		const bgfx::Memory* inds = bgfx::copy(geom.indices.data(), (uint32)geom.indices.size() * sizeof(uint32));

		if (bgfx::isValid(_vert)) {
			bgfx::update(_vert, 0, verts);
			bgfx::update(_ind, 0, inds);
		} else {
			bgfx::VertexLayout uivDecl;
			uivDecl.begin()
				.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
				.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
				.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float, true)
				.end();

			_vert = bgfx::createDynamicVertexBuffer(verts, uivDecl, BGFX_BUFFER_ALLOW_RESIZE);
			_ind = bgfx::createDynamicIndexBuffer(inds, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE);
		}

		f32 _pixelRatio = 1.0f;

		float viewMtx[16];
		bx::mtxIdentity(viewMtx);

		for (const engine::CanvasSurface& surface : geom.surfaces) {


			float projMtx[16];
			bx::mtxOrtho(projMtx, 0, (f32)canvas.getDimensions().w, (f32)canvas.getDimensions().h, 0, -1, 1, 0, bgfx::getCaps()->homogeneousDepth);

			uint64 state = 0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
				| BGFX_STATE_BLEND_ALPHA
				//| BGFX_STATE_WRITE_Z
				//| BGFX_STATE_DEPTH_TEST_LESS
				//| BGFX_STATE_CULL_CW
				//| BGFX_STATE_MSAA
				;

			switch (surface.primitive) {
			case engine::RenderPrimitive::LineList:
				state |= BGFX_STATE_PT_LINES;
				if (_lineAA) state |= BGFX_STATE_LINEAA;
				break;
			case engine::RenderPrimitive::LineStrip:
				state |= BGFX_STATE_PT_LINESTRIP;
				if (_lineAA) state |= BGFX_STATE_LINEAA;
				break;
			case engine::RenderPrimitive::Points:
				state |= BGFX_STATE_PT_POINTS;
				break;
			case engine::RenderPrimitive::TriangleStrip:
				state |= BGFX_STATE_PT_TRISTRIP;
				break;
			}

			bgfx::setViewClear(surface.viewId, BGFX_CLEAR_COLOR, 0x000000FF);
			bgfx::setViewRect(surface.viewId, 0, 0, bgfx::BackbufferRatio::Equal);
			bgfx::setViewMode(surface.viewId, bgfx::ViewMode::Sequential);
			bgfx::setViewTransform(surface.viewId, viewMtx, projMtx);

			bgfx::setTexture(0, _textureUniform, surface.texture->handle);
	
			bgfx::setVertexBuffer(0, _vert);
			bgfx::setIndexBuffer(_ind, (uint32)surface.indexOffset, (uint32)surface.indexCount);

			bgfx::setState(state);
			bgfx::submit(surface.viewId, _prog);
		}
	}
}

void BgfxRenderContext::endFrame() {
	bgfx::frame();
}
