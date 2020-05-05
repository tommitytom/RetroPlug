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
	sol::state* _state = nullptr;
	std::string _configPath;
	std::string _scriptPath;

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

	iplug::ITimeInfo* _timeInfo = nullptr;

public:
	AudioLuaContext(const std::string& configPath, const std::string& scriptPath);
	~AudioLuaContext() { shutdown(); }

	void init(ProcessingContext* ctx, iplug::ITimeInfo* timeInfo, double sampleRate);

	void closeProject();

	void addInstance(SystemIndex idx, SameBoyPlugPtr instance, const std::string& componentState);

	void duplicateInstance(SystemIndex sourceIdx, SystemIndex targetIdx, SameBoyPlugPtr instance);

	void removeInstance(SystemIndex index);

	void setActive(SystemIndex idx);

	void update(int frameCount);

	//bool onKey(const iplug::IKeyPress& key, bool down);

	void onPadButton(int button, bool down);

	void onMidi(int offset, int status, int data1, int data2);

	void onMidiClock(int button, bool down);

	void onMenu(SystemIndex idx, std::vector<Menu*>& menus);

	void onMenuResult(int id);

	void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

	std::string serializeInstance(SystemIndex index);

	std::string serializeInstances();

	void deserializeInstances(const std::string& data);

private:
	void setup();
};