#pragma once

#include <unordered_map>
#include <FileWatcher/FileWatcher.h>
#include "UiLuaContext.h"

class ChangeListener : public FW::FileWatchListener {
private:
	using hrc = std::chrono::high_resolution_clock;

	UiLuaContext* _uiCtx;
	AudioContextProxy* _proxy;
	hrc::time_point _lastUpdate;
	std::unordered_map<std::string, FW::Action> _changes;
	std::chrono::milliseconds _processDelay = std::chrono::milliseconds(50);

public:
	ChangeListener(UiLuaContext* uiContext = nullptr, AudioContextProxy* proxy = nullptr) :
		_uiCtx(uiContext), _proxy(proxy) {}

	~ChangeListener() {}

	void processChanges();

private:
	void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action);
};