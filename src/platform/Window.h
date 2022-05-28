#pragma once

#include "platform/Types.h"
#include "Application.h"

typedef struct GLFWwindow GLFWwindow;

namespace rp {
	class Window {
	private:
		GLFWwindow* _window = nullptr;
		Application* _app;
		DimensionT<uint32> _dimensions;

	public:
		Window(Application* app);
		~Window();

		void run();

		bool runFrame();

	private:
		static void renderLoopCallback(void* arg);

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
