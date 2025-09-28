#pragma once

#include "Window.h"
#include "WindowManager.h"

struct GLFWwindow;
struct GLFWcursor;
struct GLFWgamepadstate;

namespace fw::app {
	class GlfwNativeWindow : public Window {
	private:
		std::string _canvasId;
		GLFWwindow* _window = nullptr;
		GLFWwindow* _share = nullptr;
		Point _lastMousePosition = Point{0, 0};
		Dimension _dimensions;

		GLFWcursor* _cursor = nullptr;

		size_t _currentStateIdx = 0;
		GLFWgamepadstate* _gamepadState;
		f32 _axisButtonThreshold = 0.1f;
		bool _axisButtons[5 * 2] = { false }; // GLFW_GAMEPAD_AXIS_LAST = 5

	public:
		GlfwNativeWindow(ResourceManager* resourceManager, FontManager* fontManager, ViewPtr view, uint32 id, const std::string& canvasId, GLFWwindow* share);
		~GlfwNativeWindow();

		GLFWwindow* getWindow() const { return _window; }

		void makeCurrent() override;

		void setDimensions(Dimension dimensions) override;

		Dimension getDimensions() const override {
			return _dimensions;
		}

		void onCreate() override;

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;

		void onCleanup() override;

		void onFrame() override;

		void requestClose() override;

		bool shouldClose() override;

		NativeWindowHandle getNativeHandle() override;

		void focus() override;

	private:
		static void mouseEnterCallback(GLFWwindow* window, int entered);

		static void mouseMoveCallback(GLFWwindow* window, double x, double y);

		static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

		static void mouseScrollCallback(GLFWwindow* window, double x, double y);

		static void charCallback(GLFWwindow* window, unsigned int keycode);

		static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

		static void joystickCallback(int jid, int event);

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
		GlfwWindowManager(ResourceManagerPtr resourceManager, FontManagerPtr fontManager);
		~GlfwWindowManager();

		void update(std::vector<WindowPtr>& created) override;

		WindowPtr createWindow(ViewPtr view, NativeWindowHandle parent, const std::string& canvasId) override {
			GLFWwindow* share = nullptr;
			if (getWindows().size()) {
				share = std::static_pointer_cast<GlfwNativeWindow>(getWindows().at(0))->getWindow();
			}

			WindowPtr window = std::make_shared<GlfwNativeWindow>(_resourceManager.get(), _fontManager.get(), view, std::numeric_limits<uint32>::max(), canvasId, share);
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
