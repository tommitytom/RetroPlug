#pragma once

#include <bgfx/bgfx.h>

#include "foundation/Math.h"
#include "graphics/Canvas.h"
#include "graphics/RenderContext.h"

namespace fw {
	class ResourceManager;

	class BgfxRenderContext {
	private:
		void* _nativeWindowHandle;
		Dimension _resolution;

		bgfx::DynamicVertexBufferHandle _vert = BGFX_INVALID_HANDLE;
		bgfx::DynamicIndexBufferHandle _ind = BGFX_INVALID_HANDLE;

		bgfx::UniformHandle _textureUniform;
		bgfx::UniformHandle _scaleUniform;
		bgfx::UniformHandle _resolutionUniform;

		ShaderProgramHandle _defaultProgram;
		TextureHandle _defaultTexture;

		f32 _lastDelta = 0;
		f64 _totalTime = 0;

		bool _lineAA = true;

		ResourceManager& _resourceManager;

	public:
		BgfxRenderContext(void* nativeWindowHandle, Dimension res, ResourceManager& resourceManager);
		~BgfxRenderContext();

		void beginFrame(f32 delta);

		void renderCanvas(engine::Canvas& canvas);

		void endFrame();

		void cleanup();

		ShaderProgramHandle getDefaultProgram() const;

		TextureHandle getDefaultTexture() const {
			return _defaultTexture;
		}
	};

	using RenderContext = BgfxRenderContext;
}
