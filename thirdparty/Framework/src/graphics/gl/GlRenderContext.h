#pragma once

#include "foundation/Math.h"
#include "graphics/Canvas.h"
#include "graphics/RenderContext.h"

namespace fw {
	class ResourceManager;
	using NativeWindowHandle = void*;

	class GlRenderContext : public RenderContext {
	private:
		struct FrameBuffer {
			NativeWindowHandle window;
			uint32 handle;
			Dimension dimensions;
			uint32 frameLastUsed = 0;
		};

		struct ShaderUniforms {
			int32 projUniform = -1;
			int32 textureUniform = -1;
			int32 scaleUniform = -1;
			int32 resolutionUniform = -1;
		};

		std::vector<std::pair<uint32, ShaderUniforms>> _shaderUniforms;

		NativeWindowHandle _mainWindow;
		Dimension _resolution;

		uint32 _arrayBuffer = 0;
		uint32 _vertexBuffer = 0;
		uint32 _indexBuffer = 0;

		uint32 _vertexBufferSize = 0;
		uint32 _indexBufferSize = 0;

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

		void beginFrame(f32 delta) override;

		void renderCanvas(engine::Canvas& canvas, NativeWindowHandle window) override;

		void endFrame() override;

		void cleanup() override;

		std::pair<engine::ShaderDesc, engine::ShaderDesc> getDefaultShaders();

	private:
		uint32 acquireFrameBuffer(NativeWindowHandle nwh, Dimension dimensions);

		const ShaderUniforms& getShaderUniforms(uint32 programHandle);
	};
}
