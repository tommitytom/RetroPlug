#include "UiContext.h"

#include "graphics/Canvas.h"
#include "graphics/TextureAtlas.h"
#include "graphics/bgfx/BgfxShader.h"
#include "graphics/bgfx/BgfxShaderProgram.h"
#include "graphics/bgfx/BgfxTexture.h"
#include "graphics/ftgl/FtglFont.h"

using hrc = std::chrono::high_resolution_clock;
using delta_duration = std::chrono::duration<f32>;

namespace fw::app {
	UiContext::UiContext(audio::AudioManagerPtr audioManager) : _fontManager(_resourceManager), _audioManager(audioManager) {
		_windowManager = std::make_unique<GlfwWindowManager>(_resourceManager, _fontManager);
	}

	UiContext::~UiContext() {
		_audioManager = nullptr;

		_windowManager->closeAll();

		_renderContext->cleanup();
		_mainWindow->onCleanup();

		_resourceManager = ResourceManager();
		//_canvas.destroy();

		_mainWindow = nullptr;
		_renderContext = nullptr;

		_windowManager = nullptr;
	}
	
	bool UiContext::runFrame() {
		hrc::time_point time = hrc::now();
		std::chrono::nanoseconds nanoDelta = time - _lastTime;
		f32 delta = std::chrono::duration_cast<delta_duration>(nanoDelta).count();
		_lastTime = time;

		std::vector<WindowPtr> created;
		_windowManager->update(created);

		for (WindowPtr w : created) {
			if (!_mainWindow) {
				_mainWindow = w;
			}

			w->getViewManager()->createState(_audioManager);
			w->onInitialize();
		}

		std::vector<WindowPtr>& windows = _windowManager->getWindows();

		if (windows.size()) {
			_renderContext->beginFrame(delta);

			for (auto it = windows.begin(); it != windows.end(); ++it) {
				WindowPtr w = *it;

				if (!w->shouldClose()) {
					Canvas& canvas = w->getCanvas();
					canvas.setDefaults(_renderContext->getDefaultTexture(), _renderContext->getDefaultProgram(), _fontManager.loadFont("Karla-Regular", 16));
					canvas.setDimensions(w->getViewManager()->getDimensions(), 1.0f);

					w->getViewManager()->setResourceManager(&_resourceManager, &_fontManager);

					w->onUpdate(delta);

					canvas.beginRender();
					w->onRender(canvas);
					canvas.endRender();

					_renderContext->renderCanvas(canvas, w->getNativeHandle());
				}
			}

			_renderContext->endFrame();

			return !_mainWindow->shouldClose();
		}

		return false;
	}
	
	void UiContext::createRenderContext(WindowPtr window) {
		_renderContext = std::make_unique<GlRenderContext>(window->getNativeHandle(), window->getViewManager()->getDimensions(), _resourceManager);

		_resourceManager.addProvider<Font, FontProvider>();
		_resourceManager.addProvider<TextureAtlas, TextureAtlasProvider>();
		_resourceManager.addProvider<FontFace>(std::make_unique<FtglFontFaceProvider>(_resourceManager));

		//FontHandle font = _resourceManager.load<Font>("Roboto-Regular.ttf/16");

		_lastTime = hrc::now();
	}
}