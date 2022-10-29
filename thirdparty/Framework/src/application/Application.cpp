#include "Application.h"

#include <spdlog/spdlog.h>
#include <GLFW/glfw3.h>

#ifdef RP_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
static const char* s_canvas = "#canvas";
#else
#if RP_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif RP_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif RP_MACOS
#define GLFW_EXPOSE_NATIVE_COCOA
#endif

#include <GLFW/glfw3native.h>
#endif

#include "foundation/FoundationModule.h"

#include "graphics/Canvas.h"
#include "graphics/TextureAtlas.h"
#include "graphics/bgfx/BgfxFrameBuffer.h"
#include "graphics/bgfx/BgfxShader.h"
#include "graphics/bgfx/BgfxShaderProgram.h"
#include "graphics/bgfx/BgfxTexture.h"
#include "graphics/ftgl/FtglFont.h"

#include "GlfwNativeWindow.h"

using namespace fw;
using namespace fw::engine;
using namespace fw::app;

void errorCallback(int error, const char* description) {
	spdlog::error("GLFW error {}: {}", error, description);
}

Application::Application() : _fontManager(_resourceManager), _windowManager(_resourceManager, _fontManager), _canvas(_resourceManager, _fontManager) {
	FoundationModule::setup();

	glfwSetErrorCallback(errorCallback);
	
	if (!glfwInit()) {
		spdlog::critical("Failed to initialize GLFW!");
		throw std::runtime_error("Failed to initialize GLFW!");
	}

	_audioManager = std::make_shared<AudioManager>();
	_audioManager->start();
}

Application::~Application() {
	_audioManager = nullptr;

	_windowManager.closeAll();

	_renderContext->cleanup();
	_mainWindow->onCleanup();

	_resourceManager = ResourceManager();
	_canvas.destroy();

	_renderContext = nullptr;
	_mainWindow = nullptr;

	glfwTerminate();
}

void Application::createRenderContext(WindowPtr window) {
	_renderContext = std::make_unique<BgfxRenderContext>(window->getNativeHandle(), window->getViewManager()->getDimensions(), _resourceManager);

	_resourceManager.addProvider<TextureAtlas, TextureAtlasProvider>();
	_resourceManager.addProvider<Font>(std::make_unique<FtglFontProvider>(_resourceManager));

	FontHandle font = _resourceManager.load<Font>("Roboto-Regular.ttf/16");

	_canvas.setDefaults(_renderContext->getDefaultTexture(), _renderContext->getDefaultProgram(), font);
}

bool Application::runFrame() {
	f64 time = glfwGetTime();
	f32 delta = _lastTime > 0 ? (f32)(time - _lastTime) : 0.0f;
	_lastTime = time;

	// NOTE: On web this doesn't actually poll input - all input events are received BEFORE we enter runFrame
	glfwPollEvents();

	std::vector<WindowPtr> created;
	_windowManager.update(created);

	for (WindowPtr w : created) {
		if (!_mainWindow) {
			_mainWindow = w;
		}

		w->getViewManager()->getState().emplace(_audioManager);
		w->onInitialize();
	}

	std::vector<WindowPtr>& windows = _windowManager.getWindows();

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
