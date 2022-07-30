#pragma once

#include <memory>

#include "graphics/BgfxRenderContext.h"
#include "Window.h"
#include "WindowManager.h"

namespace rp::app {
	class Application {
	private:
		WindowManager _windowManager;
		std::unique_ptr<BgfxRenderContext> _renderContext;

	public:
		Application();
		~Application();

		template <typename WindowT>
		int run() {
			WindowPtr window = _windowManager.createWindow<WindowT>();
			createRenderContext(window);
			return doLoop();
		}

	private:
		void createRenderContext(WindowPtr window);

		int doLoop();
	};
}
