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
	UiLuaContext(): _state(nullptr), _proxy(nullptr) {}
	~UiLuaContext() { shutdown(); }

	void init(RetroPlugProxy* proxy, const std::string& path, const std::string& scriptPath);

	void loadRom(InstanceIndex idx, const std::string& path, GameboyModel model);
	
	void closeProject();

	void loadProject(const std::string& path);

	void saveProject(const FetchStateResponse& res);

	void removeInstance(InstanceIndex index);

	void duplicateInstance(InstanceIndex index);

	void setActive(InstanceIndex idx);

	void resetInstance(InstanceIndex idx, GameboyModel model);

	void newSav(InstanceIndex idx);
	
	void saveSav(InstanceIndex idx, const std::string& path);

	void loadSav(InstanceIndex idx, const std::string& path, bool reset);

	void update(float delta);

	bool onKey(const iplug::IKeyPress& key, bool down);

	void onPadButton(int button, bool down);

	void onDrop(const char* str);

	void onMenu(iplug::igraphics::IPopupMenu* root);

	void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

private:
	void setup();
};
