#pragma once

#include "foundation/Math.h"
#include "graphics/Canvas.h"
#include "graphics/RenderContext.h"

namespace fw {
	class ResourceManager;
	using NativeWindowHandle = void*;

	class GlRenderContext {
	private:
		struct FrameBuffer {
			NativeWindowHandle window;
			uint32 handle;
			Dimension dimensions;
			uint32 frameLastUsed = 0;
		};

		NativeWindowHandle _mainWindow;
		Dimension _resolution;

		uint32 _arrayBuffer = 0;
		uint32 _vertexBuffer = 0;
		uint32 _indexBuffer = 0;

		uint32 _vertexBufferSize = 0;
		uint32 _indexBufferSize = 0;

		int32 _projUniform = -1;
		int32 _textureUniform = -1;
		int32 _scaleUniform = -1;
		int32 _resolutionUniform = -1;

		ShaderProgramHandle _defaultProgram;
		TextureHandle _defaultTexture;

		f32 _lastDelta = 0;
		f64 _totalTime = 0;

		uint32 _viewOffset = 0;
		std::vector<FrameBuffer> _frameBuffers;
		uint32 _frameCount = 0;

		bool _lineAA = true;

		ResourceManager& _resourceManager;

	public:
		GlRenderContext(NativeWindowHandle mainWindow, Dimension res, ResourceManager& resourceManager);
		~GlRenderContext();

		void beginFrame(f32 delta);

		void renderCanvas(engine::Canvas& canvas, NativeWindowHandle window);

		void endFrame();

		void cleanup();

		ShaderProgramHandle getDefaultProgram() const;

		TextureHandle getDefaultTexture() const {
			return _defaultTexture;
		}

	private:
		uint32 acquireFrameBuffer(NativeWindowHandle nwh, Dimension dimensions);
	};

	//using RenderContext = GlRenderContext;
}
