#pragma once

#include <memory>

#include "graphics/bgfx/BgfxRenderContext.h"
#include "ui/View.h"
#include "ui/ViewManager.h"

#include "platform/Types.h"
#include "RpMath.h"
#include "core/Input.h"

namespace rp::app {
	template <typename T>
	class WindowManager;

	class Window {
	private:
		ViewManager _viewManager;
		uint32 _id;
		ViewPtr _view;

	public:
		Window(ResourceManager* resourceManager, ViewPtr view, uint32 id): _id(id), _view(view) {
			_viewManager.setResourceManager(resourceManager);
			_viewManager.setSizingPolicy(SizingPolicy::FitToContent);
			_viewManager.setDimensions(view->getDimensions());
			_viewManager.setName(view->getName());
		}
		~Window() = default;

		virtual void onCreate() {}

		virtual void onInitialize() {
			_viewManager.addChild(_view);
			_viewManager.onInitialize();
		}

		virtual void onUpdate(f32 delta) {
			_viewManager.onUpdate(delta);
		}

		virtual void onRender(engine::Canvas& canvas) {
			_viewManager.onRender(canvas);
		}

		virtual void onCleanup() {
			_view = nullptr;
			_viewManager = ViewManager();
		}

		virtual bool shouldClose() = 0;

		virtual void* getNativeHandle() = 0;

		uint32 getId() const {
			return _id;
		}

		ViewManager& getViewManager() {
			return _viewManager;
		}

		template <typename T>
		friend class WindowManager;
	};

	using WindowPtr = std::shared_ptr<Window>;
}
