#pragma once

#include "graphics/FontManager.h"
#include "graphics/RenderContext.h"

#include "Window.h"
#include "WindowManager.h"

#include "foundation/ResourceManager.h"

#include "WrappedNativeWindow.h"

#define FW_USE_SDL 1
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

		WindowPtr setup(ViewPtr view);

		WindowPtr setupNativeWindow(ViewPtr view, NativeWindowHandle nativeWindowHandle, fw::Dimension dimensions);

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
