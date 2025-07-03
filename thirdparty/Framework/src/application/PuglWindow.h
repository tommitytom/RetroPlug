#pragma once

#include <pugl/pugl.hpp>
#include <pugl/gl.hpp>

#include "Window.h"
#include "WindowManager.h"

#include "ui/View.h"

#include "foundation/ResourceProvider.h"

namespace fw::app {
	class PuglWindow : public Window {
	private:
		pugl::View _puglView;
		NativeWindowHandle _nativeWindowHandle = nullptr;
		Point _lastMousePosition;
		Dimension _size;

	public:
		PuglWindow(NativeWindowHandle nwh, ResourceManager* resourceManager, FontManager* fontManager, ViewPtr view, uint32 id, pugl::World& puglWorld)
			: Window(resourceManager, fontManager, view, id), _nativeWindowHandle(nwh), _size(view->getDimensions()), _puglView(puglWorld) {
			
		}
		~PuglWindow() = default;

		void setDimensions(Dimension dimensions) override {
			_size = dimensions;
			_puglView.setSize(dimensions.w, dimensions.h);
		}

		Dimension getDimensions() const override {
			return _size;
		}

		void setParent(NativeWindowHandle parent) {
			_puglView.setParent(reinterpret_cast<pugl::NativeView>(parent));
		}

		void show() override;

		void onCreate() override;

		void onUpdate(f32 delta) override;

		void onCleanup() override {}

		bool shouldClose() override { return false; }

		void* getNativeHandle() override { return _nativeWindowHandle; }
	};

	class PuglWindowManager final : public WindowManager {
	private:
		pugl::World _world;

	public:
		PuglWindowManager(ResourceManager& resourceManager, FontManager& fontManager);
		~PuglWindowManager();

		WindowPtr createWindow(ViewPtr view, NativeWindowHandle parent) override {
			WindowPtr window = std::make_shared<PuglWindow>(parent, &_resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max(), _world);
			addWindow(window);
			return window;
		}

		WindowPtr acquireWindow(NativeWindowHandle nativeWindowHandle, ViewPtr view) {
			return nullptr;
			//WindowPtr window = std::make_shared<PuglWindow>(nativeWindowHandle, view->getDimensions(), _resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max());
			//addWindow(window);
			//return window;
		}

		void onUpdate() override {
			_world.update(0.0); // Update the world with no timeout
		}
	};
}
