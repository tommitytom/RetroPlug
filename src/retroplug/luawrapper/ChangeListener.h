#pragma once

#include "FileWatcher/FileWatcher.h"
#include "UiLuaContext.h"

class ChangeListener : public FW::FileWatchListener {
private:
	UiLuaContext* _uiCtx;
	AudioContextProxy* _proxy;

public:
	ChangeListener(UiLuaContext* uiContext = nullptr, AudioContextProxy* proxy = nullptr) :
		_uiCtx(uiContext), _proxy(proxy) {}

	~ChangeListener() {}

	void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action);
};