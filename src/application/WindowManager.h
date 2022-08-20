#pragma once

#include <stack>
#include <vector>

#include "Window.h"

namespace rp::app {
	template <typename WindowT>
	class WindowManager {
	private:
		std::vector<WindowPtr> _windows;
		std::vector<WindowPtr> _created;
		uint32 _nextViewId = 0;
		std::stack<uint32> _availableIds;

		ResourceManager& _resourceManager;
		engine::FontManager& _fontManager;

	public:
		WindowManager(ResourceManager& resourceManager, FontManager& fontManager): _resourceManager(resourceManager), _fontManager(fontManager) {}
		~WindowManager() {}

		template <typename T>
		WindowPtr createWindow() {
			ViewPtr view = std::make_shared<T>();
			WindowPtr window = std::make_shared<WindowT>(&_resourceManager, &_fontManager, view, std::numeric_limits<uint32>::max());

			window->onCreate();
			_created.push_back(window);

			return window;
		}

		std::vector<WindowPtr>& getWindows() {
			return _windows;
		}

		void update(std::vector<WindowPtr>& created) {
			for (int32 i = (int32)_windows.size() - 1; i >= 0; --i) {
				if (_windows[i]->shouldClose()) {
					_availableIds.push(_windows[i]->getId());
					_windows.erase(_windows.begin() + i);
				}
			}

			for (WindowPtr w : _created) {
				uint32 id;
				if (_availableIds.size()) {
					id = _availableIds.top();
					_availableIds.pop();
				} else {
					id = _nextViewId++;
				}

				w->_id = id;

				_windows.push_back(w);
				created.push_back(w);
			}

			_created.clear();
		}

		void closeAll() {
			_created.clear();
			_windows.clear();
		}
	};
}
