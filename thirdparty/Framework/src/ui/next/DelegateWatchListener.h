#pragma once

#include <FileWatcher/FileWatcher.h>
#include "foundation/Types.h"

namespace fw {
	class DelegateWatchListener : public FW::FileWatchListener {
	public:
		using CallbackFunc = std::function<void(FW::WatchID, const FW::String&, const FW::String&, FW::Action)>;
		struct Change {
			FW::WatchID watchid;
			FW::String dir;
			FW::String filename;
			FW::Action action;
		};

	private:
		const f32 _defaultDelay = 0.1f;
		CallbackFunc _callback;
		std::vector<Change> _pending;
		f32 _delay = 0.0f;

	public:
		DelegateWatchListener() {}
		DelegateWatchListener(CallbackFunc&& func) : _callback(std::move(func)) {}

		void setCallback(CallbackFunc&& func) {
			_callback = std::move(func);
		}

		void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) override {
			for (auto it = _pending.begin(); it != _pending.end();) {
				if (it->watchid == watchid && it->dir == dir && it->filename == filename && it->action == action) {
					it = _pending.erase(it);
				} else {
					++it;
				}
			}

			_pending.push_back(Change{ watchid, dir, filename, action });
			_delay = _defaultDelay;
		}

		void update(f32 delta) {
			if (!_pending.empty() && _callback) {
				_delay -= delta;

				if (_delay <= 0.0f) {
					for (const auto& change : _pending) {
						_callback(change.watchid, change.dir, change.filename, change.action);
					}

					_pending.clear();
					_delay = 0.0f;
				}
			}
		}
	};
}
