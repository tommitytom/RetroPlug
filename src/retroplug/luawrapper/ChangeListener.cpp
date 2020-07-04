#include "ChangeListener.h"

void ChangeListener::handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
	fs::path p(filename);
	if (p.extension() == ".lua") {
		std::cout << "Reloading..." << std::endl;
		if (_uiCtx) {
			_uiCtx->reload();
		}

		if (_proxy) {
			_proxy->reloadLuaContext();
		}
	}
}
