#include "BgfxRenderContext.h"

#include <fstream>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include "shaders/fs_tex.h"
#include "shaders/vs_tex.h"

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

	bgfx::ShaderHandle vsh = loadShader(vs_tex, sizeof(vs_tex), "Canvas Vertex Shader");
	bgfx::ShaderHandle fsh = loadShader(fs_tex, sizeof(fs_tex), "Canvas Pixel Shader");

	_prog = bgfx::createProgram(vsh, fsh, true);

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

void BgfxRenderContext::renderCanvas(engine::BgfxCanvas& canvas) {
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

		f32 scale[4] = { (2.0f / canvas.getDimensions().w) * _pixelRatio, (2.0f / canvas.getDimensions().h) * _pixelRatio, 0.0f, 0.0f };

		for (const engine::CanvasSurface& surface : geom.surfaces) {
			uint32 state = 0
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
				//if (_lineAA) state |= BGFX_STATE_LINEAA;
				break;
			case engine::RenderPrimitive::LineStrip:
				state |= BGFX_STATE_PT_LINESTRIP;
				//if (_lineAA) state |= BGFX_STATE_LINEAA;
				break;
			case engine::RenderPrimitive::Points:
				state |= BGFX_STATE_PT_POINTS;
				break;
			case engine::RenderPrimitive::TriangleStrip:
				state |= BGFX_STATE_PT_TRISTRIP;
				break;
			}

			bgfx::touch(surface.viewId);

			bgfx::setState(state);

			bgfx::setViewClear(surface.viewId, BGFX_CLEAR_COLOR, 0x000000FF);
			bgfx::setViewRect(surface.viewId, 0, 0, bgfx::BackbufferRatio::Equal);
			//bgfx::setViewRect(surface.viewId, surface.viewArea.x, surface.viewArea.y, surface.viewArea.w, surface.viewArea.h);
			bgfx::setViewMode(surface.viewId, bgfx::ViewMode::Sequential);

			bgfx::setUniform(_scaleUniform, scale);
			bgfx::setTexture(0, _textureUniform, surface.texture->handle);
			bgfx::setVertexBuffer(0, _vert);
			bgfx::setIndexBuffer(_ind, (uint32)surface.indexOffset, (uint32)surface.indexCount);

			bgfx::submit(surface.viewId, _prog);
		}
	}
}

void BgfxRenderContext::endFrame() {
	bgfx::frame();
}
