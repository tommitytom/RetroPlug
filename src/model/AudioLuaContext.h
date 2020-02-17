#pragma once

#include <iostream>
#include <atomic>
#include "ProcessingContext.h"
#include "view/Menu.h"

namespace sol {
	class state;
};

class AudioLuaContext {
private:
	sol::state* _state;
	std::string _configPath;
	std::string _scriptPath;

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

	ProcessingContext* _context;

public:
	AudioLuaContext(ProcessingContext* ctx) : _context(ctx), _state(nullptr) {}
	~AudioLuaContext() { shutdown(); }

	void init(const std::string& configPath, const std::string& scriptPath);

	void closeProject();

	void addInstance() {}

	void addInstance(InstanceIndex idx, SameBoyPlugPtr instance);

	void removeInstance(InstanceIndex index);

	void setActive(InstanceIndex idx);

	void update();

	//bool onKey(const iplug::IKeyPress& key, bool down);

	void onPadButton(int button, bool down);

	void onMidi(int offset, int status, int data1, int data2);

	void onMidiClock(int button, bool down);

	void onMenu(std::vector<Menu*>& menus);

	void onMenuResult(int id);

	void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

private:
	void setup();
};