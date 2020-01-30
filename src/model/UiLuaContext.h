#pragma once

#include "FileWatcher/FileWatcher.h"
#include "util/xstring.h"

#include "IGraphicsStructs.h"
#include "model/RetroPlugProxy.h"

#include <iostream>

namespace sol { class state; };

class UiLuaContext {
private:
	sol::state* _state;
	std::string _configPath;
	std::string _scriptPath;

	RetroPlugProxy* _proxy;

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

public:
	UiLuaContext(): _state(nullptr) {}
	~UiLuaContext() { shutdown(); }

	void init(RetroPlugProxy* proxy, const std::string& path, const std::string& scriptPath);

	void closeProject();

	void loadProject(const std::string& path);

	void saveProject(const FetchStateResponse& res);

	void removeInstance(size_t index);

	void duplicateInstance(size_t index);

	void setActive(size_t idx);

	void update(float delta);

	void loadRom(InstanceIndex idx, const std::string& path);

	bool onKey(const iplug::igraphics::IKeyPress& key, bool down);

	void onPadButton(int button, bool down);

	void onDrop(const char* str);

	void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

private:
	void setup();
};