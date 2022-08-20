#pragma once

#include <memory>

#include "graphics/bgfx/BgfxRenderContext.h"
#include "graphics/FontManager.h"

#include "GlfwNativeWindow.h"
#include "Window.h"
#include "WindowManager.h"

#include "foundation/ResourceManager.h"
#include "audio/AudioManager.h"

namespace rp::app {
	class Application {
	private:
		WindowManager<GlfwNativeWindow> _windowManager;
		std::unique_ptr<BgfxRenderContext> _renderContext;
		f64 _lastTime = 0.0;
		engine::Canvas _canvas;

		ResourceManager _resourceManager;
		engine::FontManager _fontManager;

		FontHandle _defaultHandle;
		TextureHandle _defaultTexture;
		ShaderProgramHandle _defaultProgram;

		std::shared_ptr<AudioManager> _audioManager;

		WindowPtr _mainWindow;

	public:
		Application();
		~Application();

		template <typename ViewT>
		static int run() {
			Application app;
			app.setup<ViewT>();
			return app.doLoop();
		}

		template <typename ViewT>
		void setup() {
			WindowPtr window = _windowManager.createWindow<ViewT>();
			createRenderContext(window);

			window->getViewManager().setResourceManager(&_resourceManager, &_fontManager);
		}

		bool runFrame();

	private:
		void createRenderContext(WindowPtr window);

		int doLoop();

		static void webFrameCallback(void* arg);
	};
}
