#pragma once

#include "graphics/FontManager.h"
#include "graphics/RenderContext.h"

#include "Window.h"
#include "WindowManager.h"

#include "foundation/ResourceManager.h"

#include "WrappedNativeWindow.h"

#include "application/Config.h"

namespace fw::app {
	class UiContext {
	private:
		std::unique_ptr<WindowManager> _windowManager;
		std::unique_ptr<RenderContext> _renderContext;

		fw::ResourceManagerPtr _resourceManager;
		fw::FontManagerPtr _fontManager;

		FontFaceHandle _defaultFont;
		TextureHandle _defaultTexture;
		ShaderProgramHandle _defaultProgram;

		WindowPtr _mainWindow;

		//bool _flip = false;

	public:
		UiContext(std::unique_ptr<RenderContext>&& renderContext, std::unique_ptr<WindowManager>&& windowManager);
		~UiContext();

		bool runFrame(f32 deltaTime);

		void handleHotReload();

		WindowPtr setup(ViewPtr view, NativeWindowHandle parent = nullptr, const std::string& canvasId = "");

		WindowPtr createWindow(ViewPtr view, NativeWindowHandle parent = nullptr, const std::string& canvasId = "");

		WindowPtr setupNativeWindow(ViewPtr view, NativeWindowHandle nativeWindowHandle, fw::Dimension dimensions);

		WindowManager& getWindowManager() {
			return *_windowManager;
		}

		WindowPtr getMainWindow() {
			return _mainWindow;
		}

		void initRenderContext(WindowPtr window);
	};

	using UiContextPtr = std::shared_ptr<UiContext>;
}
