#pragma once

#include <memory>

#include "foundation/Input.h"
#include "foundation/Math.h"
#include "foundation/ResourceProvider.h"
#include "foundation/Types.h"

#include "ui/ViewManager.h"

namespace orb::app {
	using NativeWindowHandle = void*;

	class Window {
	private:
		ViewPtr _view;
		ViewManagerPtr _viewManager;
		uint32 _id;
		Canvas _canvas;
		std::function<void()> _createHandler;

	public:
		Window(ResourceManager* resourceManager, FontManager* fontManager, ViewPtr view, uint32 id): _id(id), _canvas(*resourceManager, *fontManager), _view(view) {
			_viewManager = std::make_shared<ViewManager>();
			_viewManager->setResourceManager(resourceManager, fontManager);
			_viewManager->getLayout().setDimensions(view->getDimensions());
			_viewManager->setName(view->getName());
			_viewManager->calculateLayout();

			_canvas.setDimensions(view->getDimensions(), 1.0f);
		}
		virtual ~Window() {}

		virtual void setDimensions(Dimension dimensions) = 0;

		virtual Dimension getDimensions() const = 0;

		virtual void onCreate() {}

		virtual void makeCurrent() {}

		virtual void onInitialize() {
			_viewManager->addChild(_view);
			_view->focus();
			_viewManager->onInitialize();
		}

		virtual void onUpdate(f32 delta) {
			_viewManager->onUpdate(delta);
		}

		virtual void onRender(orb::Canvas& canvas) {
			_viewManager->onRender(canvas);
		}

		virtual void onCleanup() {
			_viewManager = nullptr;
			_view = nullptr;
		}

		virtual void onFrame() {}

		virtual void show() {}

		virtual void requestClose() {}

		virtual bool shouldClose() = 0;

		virtual NativeWindowHandle getNativeHandle() = 0;

		virtual void focus() {}

		virtual void setParent(NativeWindowHandle handle) {}

		void setCreateHandler(std::function<void()>&& func) {
			_createHandler = std::move(func);
		}

		std::function<void()>& getCreateHandler() {
			return _createHandler;
		}

		Canvas& getCanvas() {
			return _canvas;
		}

		const Canvas& getCanvas() const {
			return _canvas;
		}

		uint32 getId() const {
			return _id;
		}

		const ViewPtr& getView() const {
			return _view;
		}

		const ViewManagerPtr& getViewManager() const {
			return _viewManager;
		}

		friend class WindowManager;
	};

	using WindowPtr = std::shared_ptr<Window>;
}
