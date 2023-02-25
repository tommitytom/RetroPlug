#pragma once

#include "Window.h"
#include "WindowManager.h"

struct GLFWwindow;
struct GLFWcursor;

namespace fw::app {
	class GlfwNativeWindow : public Window {
	private:
		GLFWwindow* _window = nullptr;
		Point _lastMousePosition;
		Dimension _dimensions;

		GLFWcursor* _cursor = nullptr;

	public:
		GlfwNativeWindow(ResourceManager* resourceManager, FontManager* fontManager, ViewPtr view, uint32 id)
			: Window(resourceManager, fontManager, view, id),
			_dimensions(view->getDimensions())
		{}

		~GlfwNativeWindow();

		void setDimensions(Dimension dimensions) override;

		void onCreate() override;

		void onUpdate(f32 delta) override;

		void onCleanup() override;

		void onFrame() override;

		bool shouldClose() override;

		NativeWindowHandle getNativeHandle() override;

	private:
		static void mouseEnterCallback(GLFWwindow* window, int entered);

		static void mouseMoveCallback(GLFWwindow* window, double x, double y);

		static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

		static void mouseScrollCallback(GLFWwindow* window, double x, double y);

		static void charCallback(GLFWwindow* window, unsigned int keycode) {}

		static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

		static void resizeCallback(GLFWwindow* window, int width, int height);

		static void dropCallback(GLFWwindow* window, int count, const char** paths);

		static void windowCloseCallback(GLFWwindow* window);

		static void windowRefreshCallback(GLFWwindow* window);

		static void errorCallback(int error, const char* description);
	};

	class GlfwWindowManager final : public WindowManager {
	private:
		bool _pollInput = false;

	public:
		GlfwWindowManager(ResourceManager& resourceManager, FontManager& fontManager);
		~GlfwWindowManager();

		void update(std::vector<WindowPtr>& created) override;

		WindowPtr createWindow(ViewPtr view) override {
			WindowPtr window = std::make_shared<GlfwNativeWindow>(&_resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max());
			addWindow(window);

			_pollInput = true;

			return window;
		}

		template <typename T>
		WindowPtr acquireWindow(void* nativeWindowHandle) {
			assert(false); //NYI
			return nullptr;
		}
	};
}
