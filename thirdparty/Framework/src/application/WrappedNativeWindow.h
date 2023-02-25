#pragma once

#include "Window.h"
#include "WindowManager.h"

#include "foundation/ResourceProvider.h"

namespace fw::app {
	class WrappedNativeWindow : public Window {
	private:
		NativeWindowHandle _nativeWindowHandle = nullptr;
		Point _lastMousePosition;

		fw::Dimension _size;

	public:
		WrappedNativeWindow(NativeWindowHandle nwh, fw::Dimension size, ResourceManager* resourceManager, FontManager* fontManager, ViewPtr view, uint32 id)
			: Window(resourceManager, fontManager, view, id), _nativeWindowHandle(nwh), _size(size) {}
		~WrappedNativeWindow() = default;

		void setDimensions(Dimension dimensions) override {}

		void onCreate() override;

		void onUpdate(f32 delta) override;

		void onCleanup() override {}

		bool shouldClose() override { return false; }

		void* getNativeHandle() override { return _nativeWindowHandle; }

		fw::Dimension getSize() const {
			return _size;
		}

		void setSize(fw::Dimension size) {
			_size = size;
		}
	};

	class WrappedWindowManager final : public WindowManager {
	public:
		WrappedWindowManager(ResourceManager& resourceManager, FontManager& fontManager) : WindowManager(resourceManager, fontManager) {}
		~WrappedWindowManager() = default;

		WindowPtr createWindow(ViewPtr view) override {
			assert(false); // Not available with this window manager
			return nullptr;
		}

		WindowPtr acquireWindow(NativeWindowHandle nativeWindowHandle, ViewPtr view) {
			WindowPtr window = std::make_shared<WrappedNativeWindow>(nativeWindowHandle, view->getDimensions(), &_resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max());
			addWindow(window);
			return window;
		}
	};
}
