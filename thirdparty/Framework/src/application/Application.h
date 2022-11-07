#pragma once

#include <memory>

#include "graphics/bgfx/BgfxRenderContext.h"
#include "graphics/FontManager.h"

#include "Window.h"
#include "WindowManager.h"

#include "foundation/ResourceManager.h"
#include "audio/AudioManager.h"

#include "WrappedNativeWindow.h"

#define FW_USE_GLFW
#include "application/Config.h"

namespace fw::app {
	class Application {
	private:
		std::unique_ptr<WindowManager> _windowManager;
		std::unique_ptr<BgfxRenderContext> _renderContext;
		engine::Canvas _canvas;

		std::chrono::high_resolution_clock::time_point _lastTime;

		ResourceManager _resourceManager;
		engine::FontManager _fontManager;

		FontHandle _defaultHandle;
		TextureHandle _defaultTexture;
		ShaderProgramHandle _defaultProgram;

		std::shared_ptr<audio::AudioManager> _audioManager;

		WindowPtr _mainWindow;

	public:
		Application();
		Application(std::shared_ptr<audio::AudioManager> audioManager);
		~Application();

		template <typename ViewT>
		static int run() {
			Application app;
			app.setup<ViewT>();
			return app.doLoop();
		}

		template <typename ViewT>
		WindowPtr setup() {
			auto manager = std::make_unique<WindowManagerT>(_resourceManager, _fontManager);
			WindowPtr window = manager->createWindow<ViewT>();

			_windowManager = std::move(manager);
			
			createRenderContext(window);

			window->getViewManager()->setResourceManager(&_resourceManager, &_fontManager);

			return window;
		}

		template <typename ViewT>
		WindowPtr setup(void* nativeWindowHandle, fw::Dimension dimensions) {
			_windowManager = std::make_unique<WindowManagerT>(_resourceManager, _fontManager);

			ViewPtr view = std::make_shared<ViewT>();
			WindowPtr window = std::make_shared<WrappedNativeWindow>(nativeWindowHandle, dimensions, &_resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max());
			_windowManager->addWindow(window);

			createRenderContext(window);

			window->getViewManager()->setResourceManager(&_resourceManager, &_fontManager);

			return window;
		}

		bool runFrame();

		std::shared_ptr<audio::AudioManager> getAudioManager() {
			return _audioManager;
		}

	private:
		void createRenderContext(WindowPtr window);

		int doLoop();

		static void webFrameCallback(void* arg);
	};
}
