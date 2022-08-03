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

#include "graphics/Canvas.h"
#include "GlfwNativeWindow.h"

using namespace rp;
using namespace rp::app;

const char* s_canvas = "canvas";

void errorCallback(int error, const char* description) {
	spdlog::error("GLFW error {}: {}", error, description);
}

Application::Application() {
	glfwSetErrorCallback(errorCallback);
	
	if (!glfwInit()) {
		spdlog::critical("Failed to initialize GLFW!");
		throw std::runtime_error("Failed to initialize GLFW!");
	}
}

Application::~Application() {
	_windowManager.closeAll();
	glfwTerminate();
}

void Application::createRenderContext(WindowPtr window) {
	_renderContext = std::make_unique<BgfxRenderContext>(window->getNativeHandle(), window->getView().getDimensions());
}

int Application::doLoop() {
	_windowManager.update();

	std::vector<WindowPtr>& windows = _windowManager.getWindows();
	assert(windows.size());

	rp::engine::Canvas canvas;

	while (windows.size() && !windows[0]->shouldClose()) {
		f64 time = glfwGetTime();
		f32 delta = _lastTime > 0 ? (f32)(time - _lastTime) : 0.0f;
		_lastTime = time;

		glfwPollEvents();
		
		_windowManager.update();

		_renderContext->beginFrame();

		for (auto it = windows.begin(); it != windows.end(); ++it) {
			WindowPtr w = *it;

			if (!w->shouldClose()) {
				w->onUpdate(delta);
				
				canvas.setViewId(w->getId());
				canvas.beginRender(w->getView().getDimensions(), 1.0f);
				w->onRender(canvas);
				canvas.endRender();

				_renderContext->renderCanvas(canvas);
			}
		}

		_renderContext->endFrame();
	}

	return 0;
}
