#pragma once

#include "graphics/Canvas.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

namespace fw {
	class ResourceManager;
	using ResourceManagerPtr = std::shared_ptr<ResourceManager>;
	using NativeWindowHandle = void*;

	class RenderContext {
	private:
		ResourceManagerPtr _resourceManager;
		bool _flip = true;

	public:
		RenderContext(bool flip): _flip(flip) {}
		virtual ~RenderContext() {}

		virtual void initialize(NativeWindowHandle mainWindow, Dimension res) = 0;

		virtual void beginFrame(f32 delta) = 0;

		virtual void renderCanvas(fw::Canvas& canvas, NativeWindowHandle window) = 0;

		virtual void endFrame() = 0;

		virtual void cleanup() = 0;

		virtual std::pair<fw::ShaderDesc, fw::ShaderDesc> getDefaultShaders() = 0;

		bool requiresFlip() const {
			return _flip;
		}

		void setResourceManager(ResourceManagerPtr rm) {
			_resourceManager = rm;
		}

		ResourceManagerPtr getResourceManager() {
			return _resourceManager;
		}
	};
}
