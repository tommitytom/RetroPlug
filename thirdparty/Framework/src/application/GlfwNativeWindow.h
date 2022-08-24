#pragma once

#include "Window.h"
#include "foundation/ResourceProvider.h"
#include "graphics/FrameBuffer.h"

struct GLFWwindow;

namespace fw::app {
	class GlfwNativeWindow : public Window {
	private:
		GLFWwindow* _window = nullptr;
		Point _lastMousePosition;
		FrameBufferProvider* _frameBufferProvider = nullptr;
		std::shared_ptr<FrameBuffer> _frameBuffer;

	public:
		GlfwNativeWindow(ResourceManager* resourceManager, FontManager* fontManager, ViewPtr view, uint32 id) : Window(resourceManager, fontManager, view, id) {}
		~GlfwNativeWindow();

		void onCreate() override;

		void onUpdate(f32 delta) override;

		void onCleanup() override;
		
		bool shouldClose() override;

		void* getNativeHandle() override;

		void setFrameBufferProvider(FrameBufferProvider* provider) {
			_frameBufferProvider = provider;
		}

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

		static void errorCallback(int error, const char* description);
	};
}
