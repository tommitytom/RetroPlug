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

	public:
		Application();
		~Application();

		template <typename ViewT>
		int run() {
			WindowPtr window = _windowManager.createWindow<ViewT>();
			window->onInitialize();
			createRenderContext(window);
			return doLoop();
		}

	private:
		void createRenderContext(WindowPtr window);

		int doLoop();
	};
}
