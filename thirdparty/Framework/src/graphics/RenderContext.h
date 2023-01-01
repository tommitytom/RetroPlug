#pragma once

#include "graphics/Canvas.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

namespace fw {
	class ResourceManager;
	using NativeWindowHandle = void*;

	class RenderContext {
	public:
		virtual void beginFrame(f32 delta) = 0;

		virtual void renderCanvas(engine::Canvas& canvas, NativeWindowHandle window) = 0;

		virtual void endFrame() = 0;

		virtual void cleanup() = 0;

		virtual std::pair<engine::ShaderDesc, engine::ShaderDesc> getDefaultShaders() = 0;
	};
}
