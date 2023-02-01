#include "BgfxRenderContext.h"

#include <fstream>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include "foundation/ResourceManager.h"

#include "graphics/bgfx/BgfxDefaultShaders.h"
#include "graphics/bgfx/BgfxShader.h"
#include "graphics/bgfx/BgfxShaderProgram.h"
#include "graphics/bgfx/BgfxTexture.h"

using namespace fw;
namespace fs = std::filesystem;

const bgfx::ViewId kClearView = 0;

BgfxRenderContext::BgfxRenderContext(NativeWindowHandle mainWindow, Dimension res, ResourceManager& resourceManager)
	: _mainWindow(mainWindow), _resolution(res), _resourceManager(resourceManager) {

	bgfx::PlatformData pd;
	pd.nwh = _mainWindow;
	bgfx::setPlatformData(pd);

	bgfx::Init bgfxInit;
	bgfxInit.type = bgfx::RendererType::OpenGL;
	bgfxInit.resolution.width = _resolution.w;
	bgfxInit.resolution.height = _resolution.h;
	//bgfxInit.resolution.reset = BGFX_RESET_VSYNC;// | BGFX_RESET_MSAA_X2;
	bgfxInit.platformData.nwh = _mainWindow;

	bgfx::init(bgfxInit);

	_resourceManager.addProvider<Shader, BgfxShaderProvider>();
	_resourceManager.addProvider<ShaderProgram>(std::make_unique<BgfxShaderProgramProvider>(_resourceManager.getLookup()));
	_resourceManager.addProvider<Texture, BgfxTextureProvider>();

	getDefaultShaders();

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
	for (FrameBuffer& fb : _frameBuffers) {
		bgfx::destroy(fb.handle);
	}
	_frameBuffers.clear();

	bgfx::destroy(_textureUniform);
	bgfx::destroy(_scaleUniform);
	bgfx::destroy(_resolutionUniform);
	bgfx::destroy(_vert);
	bgfx::destroy(_ind);
}

std::pair<fw::ShaderDesc, fw::ShaderDesc> BgfxRenderContext::getDefaultShaders() {
	return getDefaultBgfxShaders();
}

void BgfxRenderContext::beginFrame(f32 delta) {
	_lastDelta = delta;
	_totalTime += (f64)delta;
	_viewOffset = 0;
}

void BgfxRenderContext::renderCanvas(fw::Canvas& canvas, NativeWindowHandle window) {
	const fw::CanvasGeometry& geom = canvas.getGeometry();
	uint32 nextViewOffset = _viewOffset;

	bgfx::FrameBufferHandle frameBuffer;

	if (window == _mainWindow) {
		frameBuffer = BGFX_INVALID_HANDLE; // This sets the main window back buffer as the frame buffer

		if (canvas.getDimensions() != _resolution) {
			_resolution = canvas.getDimensions();
			bgfx::reset((uint32_t)_resolution.w, (uint32_t)_resolution.h, BGFX_RESET_VSYNC);
		}
	} else {
		frameBuffer = acquireFrameBuffer(window, canvas.getDimensions());
	}

	if (geom.vertices.size()) {
		const bgfx::Memory* verts = bgfx::copy(geom.vertices.data(), (uint32)geom.vertices.size() * sizeof(fw::CanvasVertex));
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

		for (const fw::CanvasBatch& batch : geom.batches) {
			uint32 batchViewId = _viewOffset + batch.viewId;
			assert(batchViewId <= 255);

			f32 projMtx[16];
			bx::mtxOrtho(projMtx, batch.projection.x, batch.projection.right(), batch.projection.bottom(), batch.projection.y, -1, 1, 0, bgfx::getCaps()->homogeneousDepth);

			bgfx::setViewRect(batchViewId, (uint16)batch.viewArea.x, (uint16)batch.viewArea.y, (uint16)batch.viewArea.w, (uint16)batch.viewArea.h);
			bgfx::setViewMode(batchViewId, bgfx::ViewMode::Sequential);
			bgfx::setViewTransform(batchViewId, viewMtx, projMtx);
			bgfx::setViewScissor(batchViewId, (uint16)batch.scissor.x, (uint16)batch.scissor.y, (uint16)batch.scissor.w, (uint16)batch.scissor.h);
			bgfx::setViewFrameBuffer(batchViewId, frameBuffer);

			for (const fw::CanvasSurface& surface : batch.surfaces) {
				const BgfxShaderProgram& program = surface.program.getResourceAs<BgfxShaderProgram>();
				const BgfxTexture& texture = surface.texture.getResourceAs<BgfxTexture>();

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
				case fw::RenderPrimitive::LineList:
					state |= BGFX_STATE_PT_LINES;
					if (_lineAA) state |= BGFX_STATE_LINEAA;
					break;
				case fw::RenderPrimitive::LineStrip:
					state |= BGFX_STATE_PT_LINESTRIP;
					if (_lineAA) state |= BGFX_STATE_LINEAA;
					break;
				case fw::RenderPrimitive::Points:
					state |= BGFX_STATE_PT_POINTS;
					break;
				case fw::RenderPrimitive::TriangleStrip:
					state |= BGFX_STATE_PT_TRISTRIP;
					break;
				}

				bgfx::setUniform(_resolutionUniform, dim);
				bgfx::setTexture(0, _textureUniform, texture.getBgfxHandle());

				bgfx::setVertexBuffer(0, _vert);
				bgfx::setIndexBuffer(_ind, (uint32)surface.indexOffset, (uint32)surface.indexCount);

				bgfx::setState(state);
				bgfx::submit(batchViewId, program.getBgfxHandle());

				nextViewOffset = batchViewId + 1;
			}
		}
	}

	_viewOffset = nextViewOffset;
}

void BgfxRenderContext::endFrame() {
	bgfx::frame();
	_frameCount++;

	// TODO: Tidy up old frame buffers based on FrameBuffer::frameLastUsed and _frameCount
}

bgfx::FrameBufferHandle BgfxRenderContext::acquireFrameBuffer(NativeWindowHandle window, Dimension dimensions) {
	for (FrameBuffer& frameBuffer : _frameBuffers) {
		if (frameBuffer.window == window) {
			// Frame buffer already exists for this window

			if (frameBuffer.dimensions != dimensions) {
				// Window has changed size, resize framebuffer

				bgfx::destroy(frameBuffer.handle);
				frameBuffer.handle = bgfx::createFrameBuffer(window, dimensions.w, dimensions.h);
				frameBuffer.dimensions = dimensions;
			}

			frameBuffer.frameLastUsed = _frameCount;

			return frameBuffer.handle;
		}
	}

	// Frame buffer does not exist, create a new one
	_frameBuffers.push_back(FrameBuffer{
		.window = window,
		.handle = bgfx::createFrameBuffer(window, dimensions.w, dimensions.h),
		.dimensions = dimensions,
		.frameLastUsed = _frameCount
	});

	return _frameBuffers.back().handle;
}
