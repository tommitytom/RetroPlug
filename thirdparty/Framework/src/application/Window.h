#pragma once

#include <memory>

#include "foundation/Input.h"
#include "foundation/Math.h"
#include "foundation/ResourceProvider.h"
#include "foundation/Types.h"

#include "ui/ViewManager.h"

namespace fw::app {
	using NativeWindowHandle = void*;

	class Window {
	private:
		ViewPtr _view;
		ViewManagerPtr _viewManager;
		uint32 _id;
		Canvas _canvas;

	public:
		Window(ResourceManager* resourceManager, FontManager* fontManager, ViewPtr view, uint32 id): _id(id), _canvas(*resourceManager, *fontManager), _view(view) {
			_viewManager = std::make_shared<ViewManager>();
			_viewManager->setResourceManager(resourceManager, fontManager);
			_viewManager->setSizingPolicy(view->getSizingPolicy());
			_viewManager->setDimensions(view->getDimensions());
			_viewManager->setName(view->getName());

			_canvas.setDimensions(view->getDimensions(), 1.0f);
		}
		~Window() = default;

		virtual void onCreate() {}

		virtual void onInitialize() {
			_viewManager->addChild(_view);
			_viewManager->onInitialize();
		}

		virtual void onUpdate(f32 delta) {
			_viewManager->onUpdate(delta);
		}

		virtual void onRender(engine::Canvas& canvas) {
			_viewManager->onRender(canvas);
		}

		virtual void onCleanup() {
			_viewManager.reset();
		}

		virtual void onFrame() {}

		virtual bool shouldClose() = 0;

		virtual NativeWindowHandle getNativeHandle() = 0;

		Canvas& getCanvas() {
			return _canvas;
		}

		const Canvas& getCanvas() const {
			return _canvas;
		}

		uint32 getId() const {
			return _id;
		}

		ViewManagerPtr getViewManager() {
			return _viewManager;
		}

		friend class WindowManager;
	};

	using WindowPtr = std::shared_ptr<Window>;
}
