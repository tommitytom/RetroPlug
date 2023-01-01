#pragma once

#include "graphics/FontManager.h"
#include "graphics/RenderContext.h"

#include "Window.h"
#include "WindowManager.h"

#include "foundation/ResourceManager.h"
#include "audio/AudioManager.h"

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
		engine::FontManager _fontManager;

		FontFaceHandle _defaultFont;
		TextureHandle _defaultTexture;
		ShaderProgramHandle _defaultProgram;

		WindowPtr _mainWindow;

		audio::AudioManagerPtr _audioManager;

		bool _flip = false;

	public:
		UiContext(audio::AudioManagerPtr audioManager, bool requiresFlip);
		~UiContext();

		bool runFrame();

		template <typename ViewT>
		WindowPtr setup() {
			ViewPtr view = std::make_shared<ViewT>();
			WindowPtr window = _windowManager->createWindow(view);

			createRenderContext(window);

			ViewManagerPtr vm = window->getViewManager();
			vm->setResourceManager(&_resourceManager, &_fontManager);
			vm->createState<audio::AudioManagerPtr>(_audioManager);

			return window;
		}

		template <typename ViewT>
		WindowPtr addNativeWindow(NativeWindowHandle nativeWindowHandle, fw::Dimension dimensions) {
			ViewPtr view = std::make_shared<ViewT>();

			WindowPtr window = std::make_shared<WrappedNativeWindow>(nativeWindowHandle, dimensions, &_resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max());
			_windowManager->addWindow(window);

			if (!_renderContext) {
				createRenderContext(window);
			}

			ViewManagerPtr vm = window->getViewManager();
			vm->setResourceManager(&_resourceManager, &_fontManager);
			vm->createState<audio::AudioManagerPtr>(_audioManager);

			return window;
		}

		WindowManager& getWindowManager() {
			return *_windowManager;
		}

	private:
		void createRenderContext(WindowPtr window);
	};

	using UiContextPtr = std::shared_ptr<UiContext>;
}
