#include "GlfwNativeWindow.h"

#include <GLFW/glfw3.h>

#ifdef RP_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
static const char* s_canvas = "#canvas";
#else
#if RP_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif RP_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif RP_MACOS
#define GLFW_EXPOSE_NATIVE_COCOA
#endif

#include <GLFW/glfw3native.h>
#endif

using namespace fw;
using namespace fw::app;

MouseButton::Enum convertMouseButton(int button);
VirtualKey::Enum convertKey(int key);

void* GlfwNativeWindow::getNativeHandle() {
#if RP_WEB
	return (void*)"canvas";
#elif RP_LINUX
	//init.platformData.ndt = glfwGetX11Display();
	//return (void*)(uintptr_t)glfwGetX11Window(window);
#elif RP_MACOS
	return glfwGetCocoaWindow(_window);
#elif RP_WINDOWS
	return glfwGetWin32Window(_window);
#else 
	#error "Platform not supported!"
#endif
}

void GlfwNativeWindow::mouseEnterCallback(GLFWwindow* window, int entered) {
	GlfwNativeWindow* w = static_cast<GlfwNativeWindow*>(glfwGetWindowUserPointer(window));

	if (entered > 0) {
		w->getViewManager()->onMouseEnter(w->_lastMousePosition);
	} else {
		w->getViewManager()->onMouseLeave();
	}
}

void GlfwNativeWindow::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	GlfwNativeWindow* w = static_cast<GlfwNativeWindow*>(glfwGetWindowUserPointer(window));
	w->getViewManager()->onMouseButton(MouseButtonEvent{
		.button = convertMouseButton(button),
		.down = action != GLFW_RELEASE,
		.position = w->_lastMousePosition
	});
}

void GlfwNativeWindow::mouseMoveCallback(GLFWwindow* window, f64 x, f64 y) {
	GlfwNativeWindow* w = static_cast<GlfwNativeWindow*>(glfwGetWindowUserPointer(window));
	w->_lastMousePosition = Point((int32)x, (int32)y);
	w->getViewManager()->onMouseMove(w->_lastMousePosition);
}

void GlfwNativeWindow::mouseScrollCallback(GLFWwindow* window, f64 x, f64 y) {
	GlfwNativeWindow* w = static_cast<GlfwNativeWindow*>(glfwGetWindowUserPointer(window));
	w->getViewManager()->onMouseScroll(MouseScrollEvent{
		.delta = PointF((f32)x, (f32)y),
		.position = w->_lastMousePosition
	});
}

void GlfwNativeWindow::resizeCallback(GLFWwindow* window, int x, int y) {
	// NOTE: Resizing is now handled in GlfwNativeWindow::onUpdate
}

void GlfwNativeWindow::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	GlfwNativeWindow* w = static_cast<GlfwNativeWindow*>(glfwGetWindowUserPointer(window));
	w->getViewManager()->onKey(KeyEvent{
		.key = convertKey(key),
		.down = action > 0
	});
}

void GlfwNativeWindow::dropCallback(GLFWwindow* window, int count, const char** paths) {
	GlfwNativeWindow* w = static_cast<GlfwNativeWindow*>(glfwGetWindowUserPointer(window));
	std::vector<std::string> p(count);

	for (int i = 0; i < count; ++i) {
		p[i] = paths[i];
	}

	w->getViewManager()->onDrop(p);
}

void GlfwNativeWindow::windowCloseCallback(GLFWwindow* window) {
	GlfwNativeWindow* w = static_cast<GlfwNativeWindow*>(glfwGetWindowUserPointer(window));
	//w->getViewManager()->onCloseWindowRequest();
}

void GlfwNativeWindow::windowRefreshCallback(GLFWwindow* window) {
	GlfwNativeWindow* w = static_cast<GlfwNativeWindow*>(glfwGetWindowUserPointer(window));
	//w->getViewManager()->onCloseWindowRequest();
}

#ifdef RP_WEB
EMSCRIPTEN_RESULT touchstart_callback(int eventType, const EmscriptenTouchEvent* touchEvent, void* userData) {
	Application* app = static_cast<Application*>(userData);

	for (int i = 0; i < touchEvent->numTouches; ++i) {
		app->onTouchStart((double)touchEvent->touches[i].canvasX, (double)touchEvent->touches[i].canvasY);
	}

	return 0;
}

EMSCRIPTEN_RESULT touchmove_callback(int eventType, const EmscriptenTouchEvent* touchEvent, void* userData) {
	Application* app = static_cast<Application*>(userData);

	for (int i = 0; i < touchEvent->numTouches; ++i) {
		app->onTouchStart((double)touchEvent->touches[i].canvasX, (double)touchEvent->touches[i].canvasY);
	}

	return 0;
}


EMSCRIPTEN_RESULT touchend_callback(int eventType, const EmscriptenTouchEvent* touchEvent, void* userData) {
	Application* app = static_cast<Application*>(userData);

	for (int i = 0; i < touchEvent->numTouches; ++i) {
		app->onTouchEnd((double)touchEvent->touches[i].canvasX, (double)touchEvent->touches[i].canvasY);
	}

	return 0;
}

EMSCRIPTEN_RESULT touchcancel_callback(int eventType, const EmscriptenTouchEvent* touchEvent, void* userData) {
	Application* app = static_cast<Application*>(userData);

	for (int i = 0; i < touchEvent->numTouches; ++i) {
		app->onTouchEnd((double)touchEvent->touches[i].canvasX, (double)touchEvent->touches[i].canvasY);
	}

	return 0;
}
#endif

GlfwNativeWindow::~GlfwNativeWindow() {
	if (_window) {
		glfwDestroyWindow(_window);
	}
}

void GlfwNativeWindow::onCleanup() {
	Window::onCleanup();
}

void GlfwNativeWindow::onCreate() {
	ViewManagerPtr vm = getViewManager();

	//glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	//glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

	glfwWindowHint(GLFW_RESIZABLE, vm->getSizingPolicy() != SizingPolicy::FitToContent ? GLFW_TRUE : GLFW_FALSE);
	//glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);

	Dimension dimensions = vm->getDimensions();
	_window = glfwCreateWindow(dimensions.w, dimensions.h, vm->getName().data(), NULL, NULL);

	glfwMakeContextCurrent(_window);

	glfwSetWindowUserPointer(_window, this);

	glfwSetKeyCallback(_window, keyCallback);
	glfwSetScrollCallback(_window, mouseScrollCallback);
	glfwSetCursorPosCallback(_window, mouseMoveCallback);
	glfwSetCursorEnterCallback(_window, mouseEnterCallback);
	glfwSetMouseButtonCallback(_window, mouseButtonCallback);
	glfwSetWindowSizeCallback(_window, resizeCallback);
	glfwSetDropCallback(_window, dropCallback);
	glfwSetWindowCloseCallback(_window, windowCloseCallback);
	glfwSetWindowRefreshCallback(_window, windowRefreshCallback);

#ifdef RP_WEB
	emscripten_set_touchstart_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 1, touchstart_callback);
	emscripten_set_touchstart_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 1, touchmove_callback);
	emscripten_set_touchend_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 1, touchend_callback);
	emscripten_set_touchcancel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 1, touchcancel_callback);
#endif

	//glfwSetCharCallback(_window, charCb);
	//glfwSetFramebufferSizeCallback(window, resizeCallback);
}

void GlfwNativeWindow::onUpdate(f32 delta) {
	ViewManagerPtr vm = getViewManager();

	Dimension windowSize;
	glfwGetWindowSize(_window, &windowSize.w, &windowSize.h);

	Dimension viewSize = vm->getDimensions();

	if (windowSize.w != viewSize.w || windowSize.h != viewSize.h) {
		if (vm->getSizingPolicy() == SizingPolicy::FitToContent) {
			// Resize window to fit content
			glfwSetWindowSize(_window, (int)viewSize.w, (int)viewSize.h);
		} else {
			// Resize content to fit window
			vm->setDimensions(windowSize);
		}
	}

	vm->onUpdate(delta);
	auto& shared = vm->getShared();

	glfwSwapBuffers(_window);

	if (shared.cursorChanged) {
		if (_cursor) {
			glfwDestroyCursor(_cursor);
			_cursor = nullptr;
		}

		switch (shared.cursor) {
		case CursorType::Arrow: _cursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR); break;
		case CursorType::ResizeH: _cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR); break;
		case CursorType::ResizeV: _cursor = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR); break;
		case CursorType::Hand: _cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR); break;
		case CursorType::IBeam: _cursor = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR); break;
		case CursorType::Crosshair: _cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR); break;
		}

		if (_cursor) {
			glfwSetCursor(_window, _cursor);
		}

		shared.cursorChanged = false;
	}
}

bool GlfwNativeWindow::shouldClose() {
	return glfwWindowShouldClose(_window);
}

VirtualKey::Enum convertKey(int key) {
	switch (key) {
		case GLFW_KEY_SPACE: return VirtualKey::Space;
		//case GLFW_KEY_APOSTROPHE: return VirtualKey::Apo;
		//case GLFW_KEY_COMMA: return VirtualKey::comma;
		case GLFW_KEY_MINUS: return VirtualKey::LeftCtrl;
		case GLFW_KEY_PERIOD: return VirtualKey::LeftCtrl;
		case GLFW_KEY_SLASH: return VirtualKey::LeftCtrl;
		case GLFW_KEY_0: return VirtualKey::Num0;
		case GLFW_KEY_1: return VirtualKey::Num1;
		case GLFW_KEY_2: return VirtualKey::Num2;
		case GLFW_KEY_3: return VirtualKey::Num3;
		case GLFW_KEY_4: return VirtualKey::Num4;
		case GLFW_KEY_5: return VirtualKey::Num5;
		case GLFW_KEY_6: return VirtualKey::Num6;
		case GLFW_KEY_7: return VirtualKey::Num7;
		case GLFW_KEY_8: return VirtualKey::Num8;
		case GLFW_KEY_9: return VirtualKey::Num9;
		//case GLFW_KEY_SEMICOLON: return VirtualKey::semi;
		//case GLFW_KEY_EQUAL: return VirtualKey::equa;
		case GLFW_KEY_A: return VirtualKey::A;
		case GLFW_KEY_B: return VirtualKey::B;
		case GLFW_KEY_C: return VirtualKey::C;
		case GLFW_KEY_D: return VirtualKey::D;
		case GLFW_KEY_E: return VirtualKey::E;
		case GLFW_KEY_F: return VirtualKey::F;
		case GLFW_KEY_G: return VirtualKey::G;
		case GLFW_KEY_H: return VirtualKey::H;
		case GLFW_KEY_I: return VirtualKey::I;
		case GLFW_KEY_J: return VirtualKey::J;
		case GLFW_KEY_K: return VirtualKey::K;
		case GLFW_KEY_L: return VirtualKey::L;
		case GLFW_KEY_M: return VirtualKey::M;
		case GLFW_KEY_N: return VirtualKey::N;
		case GLFW_KEY_O: return VirtualKey::O;
		case GLFW_KEY_P: return VirtualKey::P;
		case GLFW_KEY_Q: return VirtualKey::Q;
		case GLFW_KEY_R: return VirtualKey::R;
		case GLFW_KEY_S: return VirtualKey::S;
		case GLFW_KEY_T: return VirtualKey::T;
		case GLFW_KEY_U: return VirtualKey::U;
		case GLFW_KEY_V: return VirtualKey::V;
		case GLFW_KEY_W: return VirtualKey::W;
		case GLFW_KEY_X: return VirtualKey::X;
		case GLFW_KEY_Y: return VirtualKey::Y;
		case GLFW_KEY_Z: return VirtualKey::Z;
		//case GLFW_KEY_LEFT_BRACKET: return VirtualKey::leftbra;
		//case GLFW_KEY_BACKSLASH: return VirtualKey::slas;
		case GLFW_KEY_RIGHT_BRACKET: return VirtualKey::LeftCtrl;
		case GLFW_KEY_GRAVE_ACCENT: return VirtualKey::LeftCtrl;
		case GLFW_KEY_WORLD_1: return VirtualKey::LeftCtrl;
		case GLFW_KEY_WORLD_2: return VirtualKey::LeftCtrl;
		case GLFW_KEY_ESCAPE: return VirtualKey::Esc;
		case GLFW_KEY_ENTER: return VirtualKey::Enter;
		case GLFW_KEY_TAB: return VirtualKey::Tab;
		case GLFW_KEY_BACKSPACE: return VirtualKey::Backspace;
		case GLFW_KEY_INSERT: return VirtualKey::Insert;
		case GLFW_KEY_DELETE: return VirtualKey::Delete;
		case GLFW_KEY_RIGHT: return VirtualKey::RightArrow;
		case GLFW_KEY_LEFT: return VirtualKey::LeftArrow;
		case GLFW_KEY_DOWN: return VirtualKey::DownArrow;
		case GLFW_KEY_UP: return VirtualKey::UpArrow;
		case GLFW_KEY_PAGE_UP: return VirtualKey::PageUp;
		case GLFW_KEY_PAGE_DOWN: return VirtualKey::PageDown;
		case GLFW_KEY_HOME: return VirtualKey::Home;
		case GLFW_KEY_END: return VirtualKey::End;
		case GLFW_KEY_CAPS_LOCK: return VirtualKey::Caps;
		case GLFW_KEY_SCROLL_LOCK: return VirtualKey::Scroll;
		case GLFW_KEY_NUM_LOCK: return VirtualKey::NumLock;
		case GLFW_KEY_PRINT_SCREEN: return VirtualKey::PrintScreen;
		case GLFW_KEY_PAUSE: return VirtualKey::Pause;
		case GLFW_KEY_F1: return VirtualKey::F1;
		case GLFW_KEY_F2: return VirtualKey::F2;
		case GLFW_KEY_F3: return VirtualKey::F3;
		case GLFW_KEY_F4: return VirtualKey::F4;
		case GLFW_KEY_F5: return VirtualKey::F5;
		case GLFW_KEY_F6: return VirtualKey::F6;
		case GLFW_KEY_F7: return VirtualKey::F7;
		case GLFW_KEY_F8: return VirtualKey::F8;
		case GLFW_KEY_F9: return VirtualKey::F9;
		case GLFW_KEY_F10: return VirtualKey::F10;
		case GLFW_KEY_F11: return VirtualKey::F11;
		case GLFW_KEY_F12: return VirtualKey::F12;
		case GLFW_KEY_F13: return VirtualKey::F13;
		case GLFW_KEY_F14: return VirtualKey::F14;
		case GLFW_KEY_F15: return VirtualKey::F15;
		case GLFW_KEY_F16: return VirtualKey::F16;
		case GLFW_KEY_F17: return VirtualKey::F17;
		case GLFW_KEY_F18: return VirtualKey::F18;
		case GLFW_KEY_F19: return VirtualKey::F19;
		case GLFW_KEY_F20: return VirtualKey::F20;
		case GLFW_KEY_F21: return VirtualKey::F21;
		case GLFW_KEY_F22: return VirtualKey::F22;
		case GLFW_KEY_F23: return VirtualKey::F23;
		case GLFW_KEY_F24: return VirtualKey::F24;
		//case GLFW_KEY_F25: return VirtualKey::F25;
		case GLFW_KEY_KP_0: return VirtualKey::NumPad0;
		case GLFW_KEY_KP_1: return VirtualKey::NumPad1;
		case GLFW_KEY_KP_2: return VirtualKey::NumPad2;
		case GLFW_KEY_KP_3: return VirtualKey::NumPad3;
		case GLFW_KEY_KP_4: return VirtualKey::NumPad4;
		case GLFW_KEY_KP_5: return VirtualKey::NumPad5;
		case GLFW_KEY_KP_6: return VirtualKey::NumPad6;
		case GLFW_KEY_KP_7: return VirtualKey::NumPad7;
		case GLFW_KEY_KP_8: return VirtualKey::NumPad8;
		case GLFW_KEY_KP_9: return VirtualKey::NumPad9;
		case GLFW_KEY_KP_DECIMAL: return VirtualKey::Decimal;
		case GLFW_KEY_KP_DIVIDE: return VirtualKey::Divide;
		case GLFW_KEY_KP_MULTIPLY: return VirtualKey::Multiply;
		case GLFW_KEY_KP_SUBTRACT: return VirtualKey::Subtract;
		case GLFW_KEY_KP_ADD: return VirtualKey::Add;
		case GLFW_KEY_KP_ENTER: return VirtualKey::Enter;
		//case GLFW_KEY_KP_EQUAL: return VirtualKey::equal;
		case GLFW_KEY_LEFT_SHIFT: return VirtualKey::LeftShift;
		case GLFW_KEY_LEFT_CONTROL: return VirtualKey::LeftCtrl;
		case GLFW_KEY_LEFT_ALT: return VirtualKey::Alt;
		case GLFW_KEY_LEFT_SUPER: return VirtualKey::LeftWin;
		case GLFW_KEY_RIGHT_SHIFT: return VirtualKey::RightShift;
		case GLFW_KEY_RIGHT_CONTROL: return VirtualKey::RightCtrl;
		case GLFW_KEY_RIGHT_ALT: return VirtualKey::Alt;
		case GLFW_KEY_RIGHT_SUPER: return VirtualKey::RightWin;
		//case GLFW_KEY_MENU: return VirtualKey::LeftMenu;
	}

	return VirtualKey::Unknown;
}

MouseButton::Enum convertMouseButton(int button) {
	switch (button) {
	case GLFW_MOUSE_BUTTON_LEFT: return MouseButton::Left;
	case GLFW_MOUSE_BUTTON_RIGHT: return MouseButton::Right;
	case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::Middle;
	}

	return MouseButton::Unknown;
}

void errorCallback(int error, const char* description) {
	spdlog::error("GLFW error {}: {}", error, description);
}

GlfwWindowManager::GlfwWindowManager(ResourceManager& resourceManager, FontManager& fontManager) : WindowManager(resourceManager, fontManager) {
	glfwSetErrorCallback(errorCallback);

	if (!glfwInit()) {
		spdlog::critical("Failed to initialize GLFW!");
		throw std::runtime_error("Failed to initialize GLFW!");
	}
}

GlfwWindowManager::~GlfwWindowManager() {
	glfwTerminate();
}

void GlfwWindowManager::update(std::vector<WindowPtr>& created) {
	WindowManager::update(created);

	// NOTE: On web this doesn't actually poll input - all input events are received BEFORE we enter runFrame
	if (_pollInput) {
		glfwPollEvents();
	}
}
