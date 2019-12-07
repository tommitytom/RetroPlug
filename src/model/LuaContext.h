#pragma once

#include "FileWatcher/FileWatcher.h"
#include "util/xstring.h"

#include "IGraphicsStructs.h"
#include "model/RetroPlug.h"

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

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

public:
	LuaContext(): _plug(nullptr), _state(nullptr) {}
	~LuaContext() { shutdown(); }

	void init(RetroPlug* plug, const std::string& path);

	SameBoyPlugPtr addInstance(EmulatorType type);

	void removeInstance(size_t index);

	void setActive(size_t idx);

	void update(float delta);

	bool onKey(const iplug::igraphics::IKeyPress& key, bool down);

	void onPadButton(int button, bool down);

	void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

private:
	void setup();

	bool runFile(const std::string& path);

	bool runScript(const std::string& script);

	bool requireComponent(const std::string& path);
};