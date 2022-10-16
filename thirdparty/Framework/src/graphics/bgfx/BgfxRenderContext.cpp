#include "BgfxRenderContext.h"

#include <fstream>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include "foundation/ResourceManager.h"

#include "graphics/bgfx/BgfxDefaultShaders.h"
#include "graphics/bgfx/BgfxFrameBuffer.h"
#include "graphics/bgfx/BgfxShader.h"
#include "graphics/bgfx/BgfxShaderProgram.h"
#include "graphics/bgfx/BgfxTexture.h"

using namespace fw;
namespace fs = std::filesystem;

const bgfx::ViewId kClearView = 0;

BgfxRenderContext::BgfxRenderContext(void* nativeWindowHandle, Dimension res, ResourceManager& resourceManager)
	: _nativeWindowHandle(nativeWindowHandle), _resolution(res), _resourceManager(resourceManager) {

	bgfx::PlatformData pd;
	pd.nwh = _nativeWindowHandle;
	bgfx::setPlatformData(pd);

	bgfx::Init bgfxInit;
	bgfxInit.type = bgfx::RendererType::OpenGL;
	bgfxInit.resolution.width = _resolution.w;
	bgfxInit.resolution.height = _resolution.h;
	bgfxInit.resolution.reset = BGFX_RESET_VSYNC;// | BGFX_RESET_MSAA_X2;
	bgfxInit.platformData.nwh = _nativeWindowHandle;

	bgfx::init(bgfxInit);

	_resourceManager.addProvider<FrameBuffer, BgfxFrameBufferProvider>();
	_resourceManager.addProvider<Shader, BgfxShaderProvider>();
	_resourceManager.addProvider<ShaderProgram>(std::make_unique<BgfxShaderProgramProvider>(_resourceManager.getLookup()));
	_resourceManager.addProvider<Texture, BgfxTextureProvider>();
	
	TextureDesc whiteTextureDesc = TextureDesc{
		.dimensions = { 8, 8 },
		.depth = 4
	};
	
	const uint32 size = whiteTextureDesc.dimensions.w * whiteTextureDesc.dimensions.h * whiteTextureDesc.depth;
	whiteTextureDesc.data.resize(size);
	memset(whiteTextureDesc.data.data(), 0xFF, whiteTextureDesc.data.size());

	_defaultTexture = _resourceManager.create<Texture>("textures/white", whiteTextureDesc);

	auto shaderDescs = getDefaultShaders();

	_resourceManager.create<Shader>("shaders/CanvasVertex", shaderDescs.first);
	_resourceManager.create<Shader>("shaders/CanvasFragment", shaderDescs.second);

	_defaultProgram = _resourceManager.create<ShaderProgram>("shaders/CanvasDefault", {
		"shaders/CanvasVertex",
		"shaders/CanvasFragment"
	});

	_textureUniform = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
	_scaleUniform = bgfx::createUniform("scale", bgfx::UniformType::Vec4);
	_resolutionUniform = bgfx::createUniform("u_resolution", bgfx::UniformType::Vec4);

	bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x000000FF, 0.0f);
	bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
	bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
}

BgfxRenderContext::~BgfxRenderContext() {
	bgfx::shutdown();
}

void BgfxRenderContext::cleanup() {
	bgfx::destroy(_textureUniform);
	bgfx::destroy(_scaleUniform);
	bgfx::destroy(_resolutionUniform);
	bgfx::destroy(_vert);
	bgfx::destroy(_ind);
	_defaultProgram = nullptr;
	_defaultTexture = nullptr;
}

void BgfxRenderContext::beginFrame(f32 delta) {
	_lastDelta = delta;
	_totalTime += (f64)delta;
}

void BgfxRenderContext::renderCanvas(engine::Canvas& canvas) {
	const engine::CanvasGeometry& geom = canvas.getGeometry();
	
	if (geom.vertices.size()) {
		const bgfx::Memory* verts = bgfx::copy(geom.vertices.data(), (uint32)geom.vertices.size() * sizeof(engine::CanvasVertex));
		const bgfx::Memory* inds = bgfx::copy(geom.indices.data(), (uint32)geom.indices.size() * sizeof(uint32));

		if (bgfx::isValid(_vert)) {
			[[likely]]
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

		f32 viewMtx[16];
		bx::mtxIdentity(viewMtx);

		f32 dim[4] = { (f32)canvas.getDimensions().w, (f32)canvas.getDimensions().h, (f32)_totalTime, _lastDelta };
		
		for (const engine::CanvasBatch& batch : geom.batches) {
			f32 projMtx[16];
			bx::mtxOrtho(projMtx, batch.projection.x, batch.projection.right(), batch.projection.bottom(), batch.projection.y, -1, 1, 0, bgfx::getCaps()->homogeneousDepth);

			//bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x000000FF);
			//bgfx::setViewRect(batch.viewId, 0, 0, bgfx::BackbufferRatio::Equal);
			//bgfx::setViewRect(batch.viewId, 0, 0, (uint16)batch.viewArea.w, (uint16)batch.viewArea.h);
			bgfx::setViewRect(batch.viewId, (uint16)batch.viewArea.x, (uint16)batch.viewArea.y, (uint16)batch.viewArea.w, (uint16)batch.viewArea.h);
			bgfx::setViewMode(batch.viewId, bgfx::ViewMode::Sequential);
			bgfx::setViewTransform(batch.viewId, viewMtx, projMtx);
			bgfx::setViewScissor(batch.viewId, (uint16)batch.scissor.x, (uint16)batch.scissor.y, (uint16)batch.scissor.w, (uint16)batch.scissor.h);

			for (const engine::CanvasSurface& surface : batch.surfaces) {
				const ShaderProgramHandle programHandle = surface.program.isValid() ? surface.program : _defaultProgram;
				const TextureHandle textureHandle = surface.texture.isValid() ? surface.texture : _defaultTexture;

				const BgfxShaderProgram& program = programHandle.getResourceAs<BgfxShaderProgram>();
				const BgfxTexture& texture = textureHandle.getResourceAs<BgfxTexture>();

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

				bgfx::setUniform(_resolutionUniform, dim);
				bgfx::setTexture(0, _textureUniform, texture.getBgfxHandle());

				bgfx::setVertexBuffer(0, _vert);
				bgfx::setIndexBuffer(_ind, (uint32)surface.indexOffset, (uint32)surface.indexCount);

				bgfx::setState(state);
				bgfx::submit(batch.viewId, program.getBgfxHandle());
			}
		}
	}
}

void BgfxRenderContext::endFrame() {
	bgfx::frame();
}

ShaderProgramHandle BgfxRenderContext::getDefaultProgram() const {
	return _defaultProgram;
}
