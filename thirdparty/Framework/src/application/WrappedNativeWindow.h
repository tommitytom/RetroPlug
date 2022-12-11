#pragma once

#include "Window.h"
#include "WindowManager.h"

#include "foundation/ResourceProvider.h"

namespace fw::app {
	class WrappedNativeWindow : public Window {
	private:
		void* _nativeWindowHandle = nullptr;
		Point _lastMousePosition;

		fw::Dimension _size;

	public:
		WrappedNativeWindow(void* nwh, fw::Dimension size, ResourceManager* resourceManager, FontManager* fontManager, ViewPtr view, uint32 id) 
			: Window(resourceManager, fontManager, view, id), _nativeWindowHandle(nwh), _size(size) {}
		~WrappedNativeWindow() = default;

		void onCreate() override;

		void onUpdate(f32 delta) override;

		void onCleanup() override {}

		bool shouldClose() override { return false; }

		void* getNativeHandle() override { return _nativeWindowHandle; }

		fw::Dimension getSize() const {
			return _size;
		}

		fw::Dimension setSize(fw::Dimension size) {
			_size = size;
		}
	};

	class WrappedWindowManager final : public WindowManager {
	public:
		WrappedWindowManager(ResourceManager& resourceManager, FontManager& fontManager) : WindowManager(resourceManager, fontManager) {}
		~WrappedWindowManager() = default;

		template <typename T>
		WindowPtr createWindow() {
			assert(false); // Not available with this window manager
			return nullptr;
		}

		template <typename T>
		WindowPtr acquireWindow(void* nativeWindowHandle) {
			ViewPtr view = std::make_shared<T>();
			WindowPtr window = std::make_shared<WrappedNativeWindow>(nativeWindowHandle, &_resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max());
			addWindow(window);
			return window;
		}
	};
}
