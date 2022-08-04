#pragma once

#include "Window.h"

struct GLFWwindow;

namespace rp::app {
	class GlfwNativeWindow : public Window {
	private:
		GLFWwindow* _window = nullptr;
		Point _lastMousePosition;
		std::unique_ptr<FrameBuffer> _frameBuffer;

	public:
		GlfwNativeWindow(ViewPtr view, uint32 id) : Window(view, id) {}
		~GlfwNativeWindow();

		void onCreate() override;

		void onUpdate(f32 delta) override;
		
		bool shouldClose() override;

		void* getNativeHandle() override;

	private:
		static void mouseMoveCallback(GLFWwindow* window, double x, double y);

		static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

		static void mouseScrollCallback(GLFWwindow* window, double x, double y);

		static void charCallback(GLFWwindow* window, unsigned int keycode) {}

		static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

		static void resizeCallback(GLFWwindow* window, int width, int height);

		static void dropCallback(GLFWwindow* window, int count, const char** paths);

		static void windowCloseCallback(GLFWwindow* window);

		static void errorCallback(int error, const char* description);
	};
}
