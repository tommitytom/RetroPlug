#pragma once

#include <memory>


#include "foundation/Input.h"
#include "foundation/Math.h"
#include "foundation/Types.h"

#include "graphics/bgfx/BgfxRenderContext.h"

#include "ui/View.h"
#include "ui/ViewManager.h"

namespace fw::app {
	template <typename T>
	class WindowManager;

	class Window {
	private:
		ViewManagerPtr _viewManager;
		uint32 _id;
		ViewPtr _view;

	public:
		Window(ResourceManager* resourceManager, FontManager* fontManager, ViewPtr view, uint32 id): _id(id), _view(view) {
			_viewManager = std::make_shared<ViewManager>();
			_viewManager->setResourceManager(resourceManager, fontManager);
			_viewManager->setSizingPolicy(view->getSizingPolicy());
			_viewManager->setDimensions(view->getDimensions());
			_viewManager->setName(view->getName());
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
			_view = nullptr;
			_viewManager.reset();
		}

		virtual bool shouldClose() = 0;

		virtual void* getNativeHandle() = 0;

		uint32 getId() const {
			return _id;
		}

		ViewManagerPtr getViewManager() {
			return _viewManager;
		}

		template <typename T>
		friend class WindowManager;
	};

	using WindowPtr = std::shared_ptr<Window>;
}
