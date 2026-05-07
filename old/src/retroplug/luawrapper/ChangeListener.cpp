#include "ChangeListener.h"

#include <spdlog/spdlog.h>

void ChangeListener::handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
	fs::path fn = fs::path(filename);
	if (fn.extension() == ".lua") {
		fs::path fullPath = (fs::path(dir) / filename);
		std::string p = fullPath.string();

		auto found = _changes.find(p);
		if (found != _changes.end()) {
			if (found->second != action) {
				if (action == FW::Actions::Add || action == FW::Actions::Delete) {
					found->second = action;
				}
			}
		} else {
			_changes[p] = action;
		}

		_lastUpdate = hrc::now();
	}
}

void ChangeListener::processChanges() {
	if (!_changes.empty() && hrc::now() - _lastUpdate > _processDelay) {
		spdlog::info("Reloading...");

		if (_uiCtx) {
			_uiCtx->reload();
		}

		if (_proxy) {
			_proxy->reloadLuaContext();
		}

		_changes.clear();
	}	
}
