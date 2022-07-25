#include <string>
#include <filesystem>

#include <stdio.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "Game.h"

const int WINDOW_WIDTH = 1024;
const int WINDOW_HEIGHT = 768;

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	rp::Game* game = (rp::Game*)glfwGetWindowUserPointer(window);
	game->onMouseButton(button, action, mods);
}

void mouseMoveCallback(GLFWwindow* window, f64 x, f64 y) {
	rp::Game* game = (rp::Game*)glfwGetWindowUserPointer(window);
	game->onMouseMove(x, y);
}

int main(void) {
	glfwInit();
	GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Hello, bgfx!", NULL, NULL);

	bgfx::PlatformData pd;
	pd.nwh = glfwGetWin32Window(window);
	bgfx::setPlatformData(pd);

	bgfx::Init bgfxInit;
	bgfxInit.type = bgfx::RendererType::OpenGL;
	bgfxInit.resolution.width = WINDOW_WIDTH;
	bgfxInit.resolution.height = WINDOW_HEIGHT;
	bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
	bgfxInit.platformData.nwh = glfwGetWin32Window(window);
	bgfx::init(bgfxInit);

	rp::Game game;
	game.init();

	glfwSetWindowUserPointer(window, &game);
	glfwSetCursorPosCallback(window, mouseMoveCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);

	while (!glfwWindowShouldClose(window)) {
		rp::Dimension windowSize;
		glfwGetWindowSize(window, &windowSize.w, &windowSize.h);

		glfwPollEvents();

		bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000FF, 1.0f, 0);
		bgfx::setViewRect(0, 0, 0, windowSize.w, windowSize.h);

		game.update(1.0f / 60.0f);
		game.render(windowSize);

		bgfx::frame();
	}

	bgfx::shutdown();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
