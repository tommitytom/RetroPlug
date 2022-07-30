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
	void* windowHandle = nullptr;

#if RP_WEB
	windowHandle = (void*)s_canvas;
#elif RP_LINUX
	init.platformData.ndt = glfwGetX11Display();
	windowHandle = (void*)(uintptr_t)glfwGetX11Window(window);
#elif RP_MACOS
	windowHandle = glfwGetCocoaWindow(window);
#elif RP_WINDOWS
	windowHandle = glfwGetWin32Window(window->getNativeWindow());
#endif

	_renderContext = std::make_unique<BgfxRenderContext>(windowHandle, window->getDimensions());
}

int Application::doLoop() {
	_windowManager.update();

	std::vector<WindowPtr>& windows = _windowManager.getWindows();
	assert(windows.size());

	while (windows.size() && !windows[0]->isClosing()) {
		glfwPollEvents();
		
		_windowManager.update();

		_renderContext->beginFrame();

		for (auto it = windows.begin(); it != windows.end(); ++it) {
			WindowPtr w = *it;

			if (!w->isClosing()) {
				w->doFrame();
				_renderContext->renderCanvas(w->getCanvas());
			}
		}

		_renderContext->endFrame();
	}

	return 0;
}
