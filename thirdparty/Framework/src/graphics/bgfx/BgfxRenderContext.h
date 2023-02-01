#pragma once

#include <bgfx/bgfx.h>

#include "foundation/Math.h"
#include "graphics/Canvas.h"
#include "graphics/RenderContext.h"

namespace fw {
	class BgfxRenderContext : public RenderContext {
	private:
		struct FrameBuffer {
			NativeWindowHandle window;
			bgfx::FrameBufferHandle handle;
			Dimension dimensions;
			uint32 frameLastUsed = 0;
		};

		NativeWindowHandle _mainWindow;
		Dimension _resolution;

		bgfx::DynamicVertexBufferHandle _vert = BGFX_INVALID_HANDLE;
		bgfx::DynamicIndexBufferHandle _ind = BGFX_INVALID_HANDLE;

		bgfx::UniformHandle _textureUniform;
		bgfx::UniformHandle _scaleUniform;
		bgfx::UniformHandle _resolutionUniform;

		f32 _lastDelta = 0;
		f64 _totalTime = 0;

		uint32 _viewOffset = 0;
		std::vector<FrameBuffer> _frameBuffers;
		uint32 _frameCount = 0;

		bool _lineAA = true;

		ResourceManager& _resourceManager;

	public:
		BgfxRenderContext(NativeWindowHandle mainWindow, Dimension res, ResourceManager& resourceManager);
		~BgfxRenderContext();

		void beginFrame(f32 delta) override;

		void renderCanvas(fw::Canvas& canvas, NativeWindowHandle window) override;

		void endFrame() override;

		void cleanup() override;

		std::pair<fw::ShaderDesc, fw::ShaderDesc> getDefaultShaders() override;

	private:
		bgfx::FrameBufferHandle acquireFrameBuffer(NativeWindowHandle nwh, Dimension dimensions);
	};
}
