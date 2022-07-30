#pragma once

#include <memory>

#include "BgfxRenderContext.h"

#include "platform/Types.h"
#include "RpMath.h"
#include "core/Input.h"

struct GLFWwindow;

namespace rp::app {
	class WindowManager;

	class Window {
	private:
		GLFWwindow* _window = nullptr;
		Dimension _dimensions;
		PointF _lastMousePosition;
		f64 _lastTime = 0.0;
		WindowManager* _windowManager = nullptr;

		std::unique_ptr<FrameBuffer> _frameBuffer;

		uint32 _id = std::numeric_limits<uint32>::max();

		engine::BgfxCanvas _canvas;

	public:
		Window(std::string_view name, Dimension res);
		~Window();

		engine::BgfxCanvas& getCanvas() {
			return _canvas;
		}

		uint32 getId() const {
			return _id;
		}

		void setup(uint32 id, WindowManager* wm) {
			_id = id;
			_windowManager = wm;
		}

		/*void setWindowManager(WindowManager* wm) {
			_windowManager = wm;
		}*/

		WindowManager& getWindowManager() {
			return *_windowManager;
		}

		PointF getLastMousePosition() const {
			return _lastMousePosition;
		}

		Dimension getDimensions() const {
			return _dimensions;
		}

		bool isClosing();

		void doFrame();

		virtual void onInitialize() {}
		
		virtual void onFrame(f32 delta) {}		

		virtual void onKey(VirtualKey::Enum code, bool down) {}

		virtual void onChar() {}

		virtual void onMouseButton(MouseButton::Enum button, bool down) {}

		virtual void onMouseMove(rp::PointF position) {}

		virtual void onMouseWheel(rp::PointF delta) {}

		virtual void onResize(rp::Dimension size) {}

		virtual void onDrop(const std::vector<std::string_view>& paths) {}

		virtual void onMouseEnter() {}

		virtual void onMouseLeave() {}

		GLFWwindow* getNativeWindow() {
			return _window;
		}

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

		friend class WindowManager;
	};

	using WindowPtr = std::shared_ptr<Window>;
}
