#pragma once

#include "ui/View.h"

#include <sol/sol.hpp>
#include <entt/core/utility.hpp>
#include <FileWatcher/FileWatcher.h>

#include "ui/KnobView.h"
#include "ui/LabelView.h"
#include "ui/PanelView.h"
#include "ui/ButtonView.h"
#include "ui/SliderView.h"

namespace fw {
	class LuaUi;
}

SOL_BASE_CLASSES(fw::ButtonView, fw::View);
SOL_BASE_CLASSES(fw::KnobView, fw::View);
SOL_BASE_CLASSES(fw::LabelView, fw::View);
SOL_BASE_CLASSES(fw::LuaUi, fw::View);
SOL_BASE_CLASSES(fw::SliderView, fw::View);
SOL_DERIVED_CLASSES(fw::View, fw::ButtonView, fw::LabelView, fw::KnobView, fw::LuaUi, fw::SliderView);

namespace fw {
	class FileUpdateDelegate : public FW::FileWatchListener {
	private:
		std::function<void(FW::WatchID, const FW::String&, const FW::String&, FW::Action)> _func;

	public:
		FileUpdateDelegate() {}
		~FileUpdateDelegate() = default;

		void setFunc(std::function<void(FW::WatchID, const FW::String&, const FW::String&, FW::Action)>&& func) {
			_func = std::move(func);
		}

		void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
			if (_func) {
				_func(watchid, dir, filename, action);
			}
		}
	};

	class LuaUi : public View {
	private:
		sol::state _lua;
		std::string _scriptPath;
		bool _valid = false;
		
		FW::FileWatcher _watcher;
		FW::WatchID _watchId = 0;
		FileUpdateDelegate _listener;

		bool _updateValid = false;
		bool _renderValid = false;

	public:
		LuaUi();
		~LuaUi() = default;

		void setScriptPath(const std::filesystem::path& path) {
			if (_watchId != 0) {
				_watcher.removeWatch(_watchId);
			}

			_watchId = _watcher.addWatch(path.parent_path().string(), &_listener);
			_scriptPath = path.string();
			reloadScript();
		}

		void reloadScript();

		void onInitialize() override {
			if (_valid) {
				sol::protected_function f = _lua["onInitialize"];
				if (f) {
					sol::protected_function_result res = f();

					if (!res.valid()) {
						sol::error err = res;
						spdlog::error(err.what());
					}
				}
			}
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			if (_valid) {
				sol::protected_function f = _lua["onMouseButton"];
				if (f) {
					sol::protected_function_result res = f(ev);

					if (!res.valid()) {
						sol::error err = res;
						spdlog::error(err.what());
					} else {
						if (res.return_count() > 0) {
							bool handled = res;
							return handled;
						}

						return true;
					}
				}
			}

			return false;
		}

		bool onKey(const KeyEvent& ev) override {
			if (_valid) {
				sol::protected_function f = _lua["onKey"];
				if (f) {
					sol::protected_function_result res = f(ev);

					if (!res.valid()) {
						sol::error err = res;
						spdlog::error(err.what());
					} else {
						if (res.return_count() > 0) {
							bool handled = res;
							return handled;
						}

						return true;
					}
				}
			}

			return false;
		}

		void onUpdate(f32 delta) override {
			_watcher.update();

			if (_updateValid) {
				sol::protected_function f = _lua["onUpdate"];
				if (f) {
					sol::protected_function_result res = f();

					if (!res.valid()) {
						sol::error err = res;
						spdlog::error(err.what());
						_updateValid = false;
					}
				}
			}			
		}

		void onRender(Canvas& canvas) override {
			if (_renderValid) {
				sol::protected_function f = _lua["onRender"];
				if (f) {
					sol::protected_function_result res = f(canvas);

					if (!res.valid()) {
						sol::error err = res;
						spdlog::error(err.what());
						_renderValid = false;
					}
				}
			}
		}
	};
}
