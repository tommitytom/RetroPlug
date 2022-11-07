#include "Application.h"

#include <spdlog/spdlog.h>

#include "foundation/FoundationModule.h"

#include "graphics/Canvas.h"
#include "graphics/TextureAtlas.h"
#include "graphics/bgfx/BgfxFrameBuffer.h"
#include "graphics/bgfx/BgfxShader.h"
#include "graphics/bgfx/BgfxShaderProgram.h"
#include "graphics/bgfx/BgfxTexture.h"
#include "graphics/ftgl/FtglFont.h"

#include "audio/MiniAudioManager.h"

#include "GlfwNativeWindow.h"

using namespace fw;
using namespace fw::engine;
using namespace fw::app;

using hrc = std::chrono::high_resolution_clock;
using delta_duration = std::chrono::duration<f32>;

Application::Application() : _fontManager(_resourceManager), _canvas(_resourceManager, _fontManager) {
	FoundationModule::setup();

	_audioManager = std::make_shared<audio::MiniAudioManager>();
	_audioManager->start();
}

Application::Application(std::shared_ptr<audio::AudioManager> audioManager) 
	: _fontManager(_resourceManager), _canvas(_resourceManager, _fontManager), _audioManager(audioManager) 
{
	FoundationModule::setup();
	_audioManager->start();
}

Application::~Application() {
	_audioManager = nullptr;

	_windowManager->closeAll();

	_renderContext->cleanup();
	_mainWindow->onCleanup();

	_resourceManager = ResourceManager();
	_canvas.destroy();

	_mainWindow = nullptr;
	_renderContext = nullptr;

	_windowManager = nullptr;
}

void Application::createRenderContext(WindowPtr window) {
	_renderContext = std::make_unique<BgfxRenderContext>(window->getNativeHandle(), window->getViewManager()->getDimensions(), _resourceManager);

	_resourceManager.addProvider<TextureAtlas, TextureAtlasProvider>();
	_resourceManager.addProvider<Font>(std::make_unique<FtglFontProvider>(_resourceManager));

	FontHandle font = _resourceManager.load<Font>("Roboto-Regular.ttf/16");

	_canvas.setDefaults(_renderContext->getDefaultTexture(), _renderContext->getDefaultProgram(), font);

	_lastTime = hrc::now();
}

bool Application::runFrame() {
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
		//w->getViewManager()->getState().emplace(_audioManager);
		w->onInitialize();
	}

	std::vector<WindowPtr>& windows = _windowManager->getWindows();

	if (windows.size()) {
		_renderContext->beginFrame(delta);

		for (auto it = windows.begin(); it != windows.end(); ++it) {
			WindowPtr w = *it;

			if (!w->shouldClose()) {
				w->getViewManager()->setResourceManager(&_resourceManager, &_fontManager);

				w->onUpdate(delta);

				_canvas.setViewId(w->getId());
				_canvas.beginRender(w->getViewManager()->getDimensions(), 1.0f);
				w->onRender(_canvas);
				_canvas.endRender();

				_renderContext->renderCanvas(_canvas);
			}
		}

		_renderContext->endFrame();

		return !_mainWindow->shouldClose();
	}

	return false;
}

int Application::doLoop() {
#if RP_WEB
	emscripten_set_main_loop_arg(&webFrameCallback, this, 0, true);
#else
	while (runFrame()) {}
#endif

	return 0;
}

void Application::webFrameCallback(void* arg) {
	Application* app = (Application*)arg;
	app->runFrame();
}
