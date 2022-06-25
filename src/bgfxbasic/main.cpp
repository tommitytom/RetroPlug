#include <stdio.h>
#include "bgfx/bgfx.h"
#include "bgfx/platform.h"
#include "bx/math.h"
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"

#include <string>
#include <filesystem>

#include "Scene.h"

int main(void)
{
	glfwInit();
	GLFWwindow* window = glfwCreateWindow(WNDW_WIDTH, WNDW_HEIGHT, "Hello, bgfx!", NULL, NULL);

	bgfx::PlatformData pd;
	pd.nwh = glfwGetWin32Window(window);
	bgfx::setPlatformData(pd);

	bgfx::Init bgfxInit;
	bgfxInit.type = bgfx::RendererType::OpenGL;
	bgfxInit.resolution.width = WNDW_WIDTH;
	bgfxInit.resolution.height = WNDW_HEIGHT;
	bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
	bgfx::init(bgfxInit);

	bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f, 0);
	bgfx::setViewRect(0, 0, 0, WNDW_WIDTH, WNDW_HEIGHT);

	rp::Scene scene;

	while (!glfwWindowShouldClose(window)) {
		scene.render();
		glfwPollEvents();
	}

	bgfx::shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
