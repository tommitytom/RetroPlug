#pragma once

#define SOL_ALL_SAFETIES_ON 1
#include <sol/forward.hpp>
#include <entt/core/utility.hpp>
#include <FileWatcher/FileWatcher.h>

#include "ui/View.h"

namespace fw {
	class LuaUi;
}

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
		sol::state* _lua = nullptr;
		std::string _scriptPath;

		FW::FileWatcher _watcher;
		FW::WatchID _watchId = 0;
		FileUpdateDelegate _listener;

		bool _updateValid = false;
		bool _renderValid = false;

	public:
		LuaUi();
		~LuaUi();

		void setScriptPath(const std::filesystem::path& path) {
			if (_watchId != 0) {
				_watcher.removeWatch(_watchId);
			}

			_watchId = _watcher.addWatch(path.parent_path().string(), &_listener);
			_scriptPath = path.string();
			reloadScript();
		}

		void reloadScript();

		void onInitialize() override;

		bool onMouseButton(const MouseButtonEvent& ev) override;

		bool onKey(const KeyEvent& ev) override;

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;
	};
}
