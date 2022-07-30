#pragma once

#include <stack>
#include <vector>

#include "Window.h"

namespace rp::app {
	class WindowManager {
	private:
		std::vector<WindowPtr> _windows;
		std::vector<WindowPtr> _created;
		uint32 _nextViewId = 0;
		std::stack<uint32> _availableIds;

	public:
		template <typename T>
		WindowPtr createWindow() {
			WindowPtr window = std::make_shared<T>();
			_created.push_back(window);
			return window;
		}

		std::vector<WindowPtr>& getWindows() {
			return _windows;
		}

		void update() {
			for (int32 i = _windows.size() - 1; i >= 0; --i) {
				if (_windows[i]->isClosing()) {
					_availableIds.push(_windows[i]->_id);
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

				w->setup(id, this);
				w->onInitialize();

				_windows.push_back(w);
			}

			_created.clear();
		}

		void closeAll() {
			_created.clear();
			_windows.clear();
		}
	};
}