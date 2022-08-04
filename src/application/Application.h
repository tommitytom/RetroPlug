#pragma once

#include <memory>

#include "graphics/BgfxRenderContext.h"

#include "GlfwNativeWindow.h"
#include "Window.h"
#include "WindowManager.h"

namespace rp::app {
	class Application {
	private:
		WindowManager<GlfwNativeWindow> _windowManager;
		std::unique_ptr<BgfxRenderContext> _renderContext;
		f64 _lastTime = 0.0;
		engine::Canvas _canvas;

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
			window->onInitialize();
			createRenderContext(window);
			_windowManager.update();
		}

		void runFrame();

	private:
		void createRenderContext(WindowPtr window);

		int doLoop();

		static void webFrameCallback(void* arg);
	};
}
