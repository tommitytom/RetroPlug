#pragma once

#include "FileWatcher/FileWatcher.h"
#include "util/xstring.h"

#include "IGraphicsStructs.h"
#include "model/RetroPlug.h"
#include "model/RetroPlugProxy.h"

#include <iostream>

namespace sol {
	class state;
};

class RetroPlug;

class LuaContext {
private:
	sol::state* _state;
	std::string _path;

	RetroPlug* _plug;
	RetroPlugProxy* _proxy;

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

public:
	LuaContext(): _plug(nullptr), _state(nullptr) {}
	~LuaContext() { shutdown(); }

	void init(RetroPlug* plug, RetroPlugProxy* proxy, const std::string& path);

	void closeProject();

	void loadProject(const std::string& path);

	void saveProject(const FetchStateResponse& res);

	void removeInstance(size_t index);

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

	bool runFile(const std::string& path);

	bool runScript(const std::string& script, const char* error = nullptr);

	bool requireComponent(const std::string& path);
};