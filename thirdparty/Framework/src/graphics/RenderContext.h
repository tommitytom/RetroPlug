#pragma once

#include "graphics/Canvas.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

namespace fw {
	class ResourceManager;
	using NativeWindowHandle = void*;

	class RenderContext {
	public:
		RenderContext() {}
		virtual ~RenderContext() {}

		virtual void beginFrame(f32 delta) = 0;

		virtual void renderCanvas(fw::Canvas& canvas, NativeWindowHandle window) = 0;

		virtual void endFrame() = 0;

		virtual void cleanup() = 0;

		virtual std::pair<fw::ShaderDesc, fw::ShaderDesc> getDefaultShaders() = 0;
	};
}
