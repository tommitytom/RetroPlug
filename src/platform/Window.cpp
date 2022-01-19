#include "Window.h"

#include <cstdio>

#include <spdlog/spdlog.h>
#include <GLFW/glfw3.h>
#include <bx/platform.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    static const char* s_canvas = "#canvas";
#else
    #if BX_PLATFORM_LINUX
        #define GLFW_EXPOSE_NATIVE_X11
    #elif BX_PLATFORM_WINDOWS
        #define GLFW_EXPOSE_NATIVE_WIN32
    #elif BX_PLATFORM_OSX
        #define GLFW_EXPOSE_NATIVE_COCOA
    #endif

    #include <GLFW/glfw3native.h>
#endif

#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bgfx/defines.h>
#include <bgfx/platform.h>

using namespace rp;

Window::Window(Application* app): _app(app) {
	glfwSetErrorCallback(errorCallback);

	if (!glfwInit()) {
		return;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);

	_dimensions = app->getResolution();

	GLFWwindow* window = glfwCreateWindow(_dimensions.w, _dimensions.h, app->getName().c_str(), 0, 0);
	if (!window) {
		return;
	}

	//glfwSetWindowPos(window, p.x, p.y);

	_window = window;
	glfwSetWindowUserPointer(window, app);
	glfwSetFramebufferSizeCallback(window, resizeCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);
	glfwSetCursorPosCallback(window, mouseMoveCallback);
	glfwSetKeyCallback(window, keyCallback);
	glfwSetCharCallback(window, charCallback);
	glfwSetScrollCallback(window, mouseScrollCallback);
	glfwSetDropCallback(window, dropCallback);

	// Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
	// Most graphics APIs must be used on the same thread that created the window.
	//bgfx::renderFrame();

	bgfx::Init init;
#if BX_PLATFORM_EMSCRIPTEN
	init.platformData.nwh = (void*)s_canvas;
#elif BX_PLATFORM_LINUX || BX_PLATFORM_BSD
	init.platformData.ndt = glfwGetX11Display();
	init.platformData.nwh = (void*)(uintptr_t)glfwGetX11Window(window);
#elif BX_PLATFORM_OSX
	init.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
	init.platformData.nwh = glfwGetWin32Window(window);
#endif

	int width, height;
	glfwGetWindowSize(window, &width, &height);
	init.resolution.width = (uint32_t)width;
	init.resolution.height = (uint32_t)height;
	init.resolution.reset = BGFX_RESET_VSYNC;

	if (!bgfx::init(init)) {
		spdlog::critical("Failed to initialize BGFX!");
		exit(-1);
	}

	spdlog::info("BGFX initialized!");
}

Window::~Window() {
	bgfx::shutdown();
	glfwTerminate();
}

void Window::run() {
	_app->onInit();

#if BX_PLATFORM_EMSCRIPTEN
	emscripten_set_main_loop_arg(&renderLoopCallback, _window, 0, true);
#else
	while (!glfwWindowShouldClose(_window)) {
		renderLoopCallback(_window);
	}
#endif
}

bool Window::runFrame() {
	renderLoopCallback(_window);
	return !glfwWindowShouldClose(_window);
}

void Window::renderLoopCallback(void* arg) {
	GLFWwindow* window = static_cast<GLFWwindow*>(arg);
	Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));

	if (app->hasResized()) {
		Dimension<uint32> res = app->getResolution();
		glfwSetWindowSize(window, res.w, res.h);
	}

	// NOTE: On web this doesn't actually poll input - all input events are received BEFORE we enter renderLoopCallback
	glfwPollEvents();

	app->handleFrame(glfwGetTime());

	if (app->_closeRequested) {
		glfwSetWindowShouldClose(window, true);
	}
}

void Window::mouseMoveCallback(GLFWwindow* window, double x, double y) {
	Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
	app->handleMouseMove(x, y);
}

void Window::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
	app->handleMouseButton(button, action, mods);
}

void Window::mouseScrollCallback(GLFWwindow* window, double x, double y) {
	Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
	app->handleMouseScroll(x, y);
}

void Window::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
	app->handleKey(key, scancode, action, mods);
}

void Window::resizeCallback(GLFWwindow* window, int width, int height) {
	Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
	app->handleResize(width, height);
}

void Window::dropCallback(GLFWwindow* window, int count, const char** paths) {
	Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
	app->handleDrop(count, paths);
}

void Window::errorCallback(int error, const char* description) {
	spdlog::error("GLFW error {}: {}", error, description);
}
