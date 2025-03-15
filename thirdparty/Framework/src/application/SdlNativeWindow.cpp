#include "SdlNativeWindow.h"

#include <SDL.h>
#include <SDL_syswm.h>

namespace fw::app {

	//MouseButton convertSdlMouseButton(int button);
	VirtualKey convertSdlKey(int key);

	void* SdlNativeWindow::getNativeHandle() {
		SDL_SysWMinfo wmInfo;
		SDL_VERSION(&wmInfo.version);
		SDL_GetWindowWMInfo(_window, &wmInfo);
		return wmInfo.info.win.window;
	}

	/*#ifdef FW_PLATFORM_WEB
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
	#endif*/

	SdlNativeWindow::~SdlNativeWindow() {
		if (_window) {
			SDL_DestroyWindow(_window);
			_window = nullptr;
		}
	}

	void SdlNativeWindow::onCleanup() {
		Window::onCleanup();
	}

	void SdlNativeWindow::onCreate() {
		ViewManagerPtr vm = getViewManager();
		Dimension dimensions = vm->getDimensions();

		_window = SDL_CreateWindow(vm->getName().data(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, dimensions.w, dimensions.h, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
		_sdlWindowId = SDL_GetWindowID(_window);

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetSwapInterval(0);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

		auto glc = SDL_GL_CreateContext(_window);
		auto rdr = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);

		//glfwMakeContextCurrent(_window);
		//glfwSetWindowUserPointer(_window, this);

		/*
		#ifdef FW_PLATFORM_WEB
			emscripten_set_touchstart_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 1, touchstart_callback);
			emscripten_set_touchstart_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 1, touchmove_callback);
			emscripten_set_touchend_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 1, touchend_callback);
			emscripten_set_touchcancel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, 1, touchcancel_callback);
		#endif*/
	}

	void SdlNativeWindow::onFrame() {
		SDL_GL_SwapWindow(_window);
	}

	void SdlNativeWindow::setDimensions(Dimension dimensions) {
		_dimensions = dimensions;
	}

	void SdlNativeWindow::onUpdate(f32 delta) {
		ViewManagerPtr vm = getViewManager();

		Dimension viewSize = vm->getDimensions();
		viewSize = vm->getDimensions();

		if (viewSize.w > 0 && viewSize.h > 0 && (_dimensions.w != viewSize.w || _dimensions.h != viewSize.h)) {
			_dimensions = viewSize;
			//vm->getLayout().setDimensions(_dimensions);
			//glfwSetWindowSize(_window, (int)viewSize.w, (int)viewSize.h);
			SDL_SetWindowSize(_window, (int)viewSize.w, (int)viewSize.h);

			/*if (vm->getSizingPolicy() == SizingPolicy::FitToContent) {
				// Resize window to fit content
				glfwSetWindowSize(_window, (int)viewSize.w, (int)viewSize.h);
				_dimensions = viewSize;
			} else {
				// Resize content to fit window
				vm->setDimensions(_dimensions);
			}*/
		}

		vm->onUpdate(delta);
		auto& shared = vm->getShared();

		if (shared.cursorChanged) {
			/*if (_cursor) {
				glfwDestroyCursor(_cursor);
				_cursor = nullptr;
			}

			// This is not ideal since GLFW only provides a very small set of cursors to use
			switch (shared.cursor) {
			case CursorType::ResizeNS:
			case CursorType::ResizeNE:
			case CursorType::ResizeNW:
			case CursorType::ResizeSE:
			case CursorType::ResizeSW:
				_cursor = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
				break;
			case CursorType::Arrow: _cursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR); break;
			case CursorType::ResizeEW: _cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR); break;
			case CursorType::Hand: _cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR); break;
			case CursorType::IBeam: _cursor = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR); break;
			case CursorType::Crosshair: _cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR); break;
			}

			if (_cursor) {
				glfwSetCursor(_window, _cursor);
			}

			shared.cursorChanged = false;
			*/
		}

		_shouldClose = vm->shouldClose();
	}

	bool SdlNativeWindow::shouldClose() {
		return _shouldClose;
	}

	VirtualKey convertSdlKey(SDL_Keycode key) {
		switch (key) {
			case SDLK_SPACE: return VirtualKey::Space;
			case SDLK_MINUS: return VirtualKey::LeftCtrl;
			case SDLK_PERIOD: return VirtualKey::LeftCtrl;
			case SDLK_SLASH: return VirtualKey::LeftCtrl;
			case SDLK_0: return VirtualKey::Num0;
			case SDLK_1: return VirtualKey::Num1;
			case SDLK_2: return VirtualKey::Num2;
			case SDLK_3: return VirtualKey::Num3;
			case SDLK_4: return VirtualKey::Num4;
			case SDLK_5: return VirtualKey::Num5;
			case SDLK_6: return VirtualKey::Num6;
			case SDLK_7: return VirtualKey::Num7;
			case SDLK_8: return VirtualKey::Num8;
			case SDLK_9: return VirtualKey::Num9;
			case SDLK_a: return VirtualKey::A;
			case SDLK_b: return VirtualKey::B;
			case SDLK_c: return VirtualKey::C;
			case SDLK_d: return VirtualKey::D;
			case SDLK_e: return VirtualKey::E;
			case SDLK_f: return VirtualKey::F;
			case SDLK_g: return VirtualKey::G;
			case SDLK_h: return VirtualKey::H;
			case SDLK_i: return VirtualKey::I;
			case SDLK_j: return VirtualKey::J;
			case SDLK_k: return VirtualKey::K;
			case SDLK_l: return VirtualKey::L;
			case SDLK_m: return VirtualKey::M;
			case SDLK_n: return VirtualKey::N;
			case SDLK_o: return VirtualKey::O;
			case SDLK_p: return VirtualKey::P;
			case SDLK_q: return VirtualKey::Q;
			case SDLK_r: return VirtualKey::R;
			case SDLK_s: return VirtualKey::S;
			case SDLK_t: return VirtualKey::T;
			case SDLK_u: return VirtualKey::U;
			case SDLK_v: return VirtualKey::V;
			case SDLK_w: return VirtualKey::W;
			case SDLK_x: return VirtualKey::X;
			case SDLK_y: return VirtualKey::Y;
			case SDLK_z: return VirtualKey::Z;
			case SDLK_RIGHTBRACKET: return VirtualKey::LeftCtrl;
			case SDLK_BACKQUOTE: return VirtualKey::LeftCtrl;
			case SDLK_ESCAPE: return VirtualKey::Esc;
			case SDLK_RETURN: return VirtualKey::Enter;
			case SDLK_TAB: return VirtualKey::Tab;
			case SDLK_BACKSPACE: return VirtualKey::Backspace;
			case SDLK_INSERT: return VirtualKey::Insert;
			case SDLK_DELETE: return VirtualKey::Delete;
			case SDLK_RIGHT: return VirtualKey::RightArrow;
			case SDLK_LEFT: return VirtualKey::LeftArrow;
			case SDLK_DOWN: return VirtualKey::DownArrow;
			case SDLK_UP: return VirtualKey::UpArrow;
			case SDLK_PAGEUP: return VirtualKey::PageUp;
			case SDLK_PAGEDOWN: return VirtualKey::PageDown;
			case SDLK_HOME: return VirtualKey::Home;
			case SDLK_END: return VirtualKey::End;
			case SDLK_CAPSLOCK: return VirtualKey::Caps;
			case SDLK_SCROLLLOCK: return VirtualKey::Scroll;
			case SDLK_NUMLOCKCLEAR: return VirtualKey::NumLock;
			case SDLK_PRINTSCREEN: return VirtualKey::PrintScreen;
			case SDLK_PAUSE: return VirtualKey::Pause;
			case SDLK_F1: return VirtualKey::F1;
			case SDLK_F2: return VirtualKey::F2;
			case SDLK_F3: return VirtualKey::F3;
			case SDLK_F4: return VirtualKey::F4;
			case SDLK_F5: return VirtualKey::F5;
			case SDLK_F6: return VirtualKey::F6;
			case SDLK_F7: return VirtualKey::F7;
			case SDLK_F8: return VirtualKey::F8;
			case SDLK_F9: return VirtualKey::F9;
			case SDLK_F10: return VirtualKey::F10;
			case SDLK_F11: return VirtualKey::F11;
			case SDLK_F12: return VirtualKey::F12;
			case SDLK_F13: return VirtualKey::F13;
			case SDLK_F14: return VirtualKey::F14;
			case SDLK_F15: return VirtualKey::F15;
			case SDLK_F16: return VirtualKey::F16;
			case SDLK_F17: return VirtualKey::F17;
			case SDLK_F18: return VirtualKey::F18;
			case SDLK_F19: return VirtualKey::F19;
			case SDLK_F20: return VirtualKey::F20;
			case SDLK_F21: return VirtualKey::F21;
			case SDLK_F22: return VirtualKey::F22;
			case SDLK_F23: return VirtualKey::F23;
			case SDLK_F24: return VirtualKey::F24;
			case SDLK_KP_0: return VirtualKey::NumPad0;
			case SDLK_KP_1: return VirtualKey::NumPad1;
			case SDLK_KP_2: return VirtualKey::NumPad2;
			case SDLK_KP_3: return VirtualKey::NumPad3;
			case SDLK_KP_4: return VirtualKey::NumPad4;
			case SDLK_KP_5: return VirtualKey::NumPad5;
			case SDLK_KP_6: return VirtualKey::NumPad6;
			case SDLK_KP_7: return VirtualKey::NumPad7;
			case SDLK_KP_8: return VirtualKey::NumPad8;
			case SDLK_KP_9: return VirtualKey::NumPad9;
			case SDLK_KP_PERIOD: return VirtualKey::Decimal;
			case SDLK_KP_DIVIDE: return VirtualKey::Divide;
			case SDLK_KP_MULTIPLY: return VirtualKey::Multiply;
			case SDLK_KP_MINUS: return VirtualKey::Subtract;
			case SDLK_KP_PLUS: return VirtualKey::Add;
			case SDLK_KP_ENTER: return VirtualKey::Enter;
			case SDLK_LSHIFT: return VirtualKey::LeftShift;
			case SDLK_LCTRL: return VirtualKey::LeftCtrl;
			case SDLK_LALT: return VirtualKey::Alt;
			case SDLK_LGUI: return VirtualKey::LeftWin;
			case SDLK_RSHIFT: return VirtualKey::RightShift;
			case SDLK_RCTRL: return VirtualKey::RightCtrl;
			case SDLK_RALT: return VirtualKey::Alt;
			case SDLK_RGUI: return VirtualKey::RightWin;
		}

		return VirtualKey::Unknown;
	}

	MouseButton convertSdlMouseButton(Uint32 button) {
		
		switch (button) {
		case SDL_BUTTON_LEFT: return MouseButton::Left;
		case SDL_BUTTON_RIGHT: return MouseButton::Right;
		case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
		}

		return MouseButton::Unknown;
	}

	inline void errorCallback(int error, const char* description) {
		spdlog::error("SDL error {}: {}", error, description);
	}

	SdlWindowManager::SdlWindowManager(ResourceManager& resourceManager, FontManager& fontManager) : WindowManager(resourceManager, fontManager) {
	}

	SdlWindowManager::~SdlWindowManager() {
	}

	void SdlWindowManager::update(std::vector<WindowPtr>& created) {
		WindowManager::update(created);

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
			{
				auto window = findSdlWindow(event.motion.windowID);
				if (!window) {
					window = std::static_pointer_cast<SdlNativeWindow>(getWindows()[0]);
				}

				CloseWindowContext ctx;
				window->getViewManager()->onCloseWindowRequest(ctx);
				window->_shouldClose = ctx.closing;
				break;
			}
			case SDL_MOUSEMOTION:
			{
				const auto window = findSdlWindow(event.motion.windowID);
				window->_lastMousePosition = Point(event.motion.x, event.motion.y);
				window->getViewManager()->onMouseMove(window->_lastMousePosition);
				break;
			}

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
			{
				const auto window = findSdlWindow(event.button.windowID);
				window->_lastMousePosition.x = event.button.x;
				window->_lastMousePosition.y = event.button.y;

				window->getViewManager()->onMouseButton(MouseButtonEvent{
					.button = convertSdlMouseButton(event.button.button),
					.down = event.button.state == SDL_PRESSED,
					.position = window->_lastMousePosition
				});

				break;
			}
			case SDL_MOUSEWHEEL:
			{
				const auto window = findSdlWindow(event.wheel.windowID);
				window->getViewManager()->onMouseScroll(MouseScrollEvent{
					.delta = PointF((f32)event.wheel.x, (f32)event.wheel.y),
					.position = window->_lastMousePosition
				});
				break;
			}
			case SDL_KEYDOWN:
			case SDL_KEYUP:
			{
				const auto window = findSdlWindow(event.key.windowID);

				KeyAction action = KeyAction::Press;
				if (event.key.repeat) {
					action = KeyAction::Repeat;
				} else if (event.type == SDL_KEYUP) {
					action = KeyAction::Release;
				}
				
				const VirtualKey key = convertSdlKey(event.key.keysym.sym);

				window->getViewManager()->onKey(KeyEvent{
					.action = action,
					.key = key,
					.down = action != KeyAction::Release,

					// Whats all this then?
					.action2 = (uint32)action,
					.key2 = (uint32)key
				});

				break;
			}
			case SDL_TEXTINPUT:
			{
				const auto window = findSdlWindow(event.text.windowID);
				window->getViewManager()->onChar(CharEvent{
					.keyCode = (unsigned int)event.text.text[0]  // Note: This is simplified, might need UTF-8 handling
				});
				break;
			}
			case SDL_DROPFILE:
			{
				const auto window = findSdlWindow(event.drop.windowID);
				std::vector<std::string> paths;
				paths.push_back(event.drop.file);
				window->getViewManager()->onDrop(paths);
				SDL_free(event.drop.file);  // SDL requires us to free this
				break;
			}
			case SDL_WINDOWEVENT:
			{
				// Get the window ID from the event
				const auto window = findSdlWindow(event.window.windowID);

				if (!window) {
					spdlog::warn("Failed to process event: Window could not be found!");
					continue;
				}

				switch (event.window.event) {
				case SDL_WINDOWEVENT_ENTER:
				{
					window->getViewManager()->onMouseEnter(window->_lastMousePosition);
					break;
				}
				case SDL_WINDOWEVENT_LEAVE:
				{
					window->getViewManager()->onMouseLeave();
					break;
				}
				case SDL_WINDOWEVENT_CLOSE:
				{
					// See SDL_QUIT
					break;
				}
				case SDL_WINDOWEVENT_RESIZED:
				{
					window->setDimensions({ (int32)event.window.data1, (int32)event.window.data2 });
					break;
				}
				}
				break;
			}
			}
		}
	}


	std::shared_ptr<SdlNativeWindow> SdlWindowManager::findSdlWindow(uint32 id) {
		for (const auto& window : getWindows()) {
			auto w = std::static_pointer_cast<SdlNativeWindow>(window);
			if (w->getSdlWindowId() == id) {
				return w;
			}
		}

		return nullptr;
	}
}
