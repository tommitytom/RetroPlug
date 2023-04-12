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

		std::shared_ptr<ResourceManager> _resourceManager;
		fw::FontManager _fontManager;

		FontFaceHandle _defaultFont;
		TextureHandle _defaultTexture;
		ShaderProgramHandle _defaultProgram;

		WindowPtr _mainWindow;

		//bool _flip = false;

	public:
		UiContext(std::unique_ptr<RenderContext>&& renderContext);
		~UiContext();

		bool runFrame();

		void handleHotReload();

		WindowPtr setup(ViewPtr view) {
			WindowPtr window = _windowManager->createWindow(view);
			initRenderContext(window);
			
			ViewManagerPtr vm = window->getViewManager();
			vm->setResourceManager(_resourceManager.get(), &_fontManager);

			_mainWindow = window;

			return window;
		}

		WindowPtr setupNativeWindow(ViewPtr view, NativeWindowHandle nativeWindowHandle, fw::Dimension dimensions) {
			WindowPtr window = std::make_shared<WrappedNativeWindow>(nativeWindowHandle, dimensions, _resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max());
			_windowManager->addWindow(window);

			initRenderContext(window);

			ViewManagerPtr vm = window->getViewManager();
			vm->setResourceManager(_resourceManager.get(), &_fontManager);

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
		void initRenderContext(WindowPtr window);
	};

	using UiContextPtr = std::shared_ptr<UiContext>;
}
