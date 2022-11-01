#pragma once

#include <stack>
#include <vector>

#include "Window.h"

namespace fw::app {
	class WindowManager {
	private:
		std::vector<WindowPtr> _windows;
		std::vector<WindowPtr> _created;
		uint32 _nextViewId = 0;
		std::stack<uint32> _availableIds;

	protected:
		ResourceManager& _resourceManager;
		engine::FontManager& _fontManager;

	public:
		WindowManager(ResourceManager& resourceManager, FontManager& fontManager): _resourceManager(resourceManager), _fontManager(fontManager) {}
		~WindowManager() {}

		void addWindow(WindowPtr window) {
			window->onCreate();
			_created.push_back(window);
		}

		virtual void update(std::vector<WindowPtr>& created) {
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

		std::vector<WindowPtr>& getWindows() {
			return _windows;
		}
	};
}
