#pragma once

#include "graphics/FontManager.h"
#include "graphics/RenderContext.h"

#include "Window.h"
#include "WindowManager.h"

#include "foundation/ResourceManager.h"

#include "WrappedNativeWindow.h"

#define FW_USE_GLFW
#include "application/Config.h"

namespace fw::app {
	class UiContext {
	private:
		std::unique_ptr<WindowManager> _windowManager;
		std::unique_ptr<RenderContext> _renderContext;

		std::chrono::high_resolution_clock::time_point _lastTime;

		ResourceManager _resourceManager;
		fw::FontManager _fontManager;

		FontFaceHandle _defaultFont;
		TextureHandle _defaultTexture;
		ShaderProgramHandle _defaultProgram;

		WindowPtr _mainWindow;

		bool _flip = false;

	public:
		UiContext(bool requiresFlip);
		~UiContext();

		bool runFrame();

		WindowPtr setup(ViewPtr view) {
			WindowPtr window = _windowManager->createWindow(view);

			createRenderContext(window);

			ViewManagerPtr vm = window->getViewManager();
			vm->setResourceManager(&_resourceManager, &_fontManager);

			_mainWindow = window;

			return window;
		}

		WindowPtr addNativeWindow(ViewPtr view, NativeWindowHandle nativeWindowHandle, fw::Dimension dimensions) {
			WindowPtr window = std::make_shared<WrappedNativeWindow>(nativeWindowHandle, dimensions, &_resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max());
			_windowManager->addWindow(window);

			if (!_renderContext) {
				createRenderContext(window);
			}

			ViewManagerPtr vm = window->getViewManager();
			vm->setResourceManager(&_resourceManager, &_fontManager);

			if (!_mainWindow) {
				_mainWindow = window;
			}

			return window;
		}

		WindowManager& getWindowManager() {
			return *_windowManager;
		}

		WindowPtr getMainWindow() {
			return _mainWindow;
		}

	private:
		void createRenderContext(WindowPtr window);
	};

	using UiContextPtr = std::shared_ptr<UiContext>;
}
