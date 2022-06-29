#include <string>
#include <filesystem>

#include <stdio.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "Scene.h"

const int WINDOW_WIDTH = 1024;
const int WINDOW_HEIGHT = 768;

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

	rp::Scene scene;
	scene.init();

	while (!glfwWindowShouldClose(window)) {
		rp::Dimension windowSize;
		glfwGetWindowSize(window, &windowSize.w, &windowSize.h);

		glfwPollEvents();
		
		bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x000000FF, 1.0f, 0);
		bgfx::setViewRect(0, 0, 0, windowSize.w, windowSize.h);

		scene.update(1.0f / 60.0f);
		scene.render(windowSize);

		bgfx::frame();
	}

	bgfx::shutdown();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
