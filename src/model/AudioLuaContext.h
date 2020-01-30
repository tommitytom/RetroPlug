#pragma once

#include <iostream>
#include <atomic>
#include "ProcessingContext.h"

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
	AudioLuaContext() : _context(nullptr), _state(nullptr) {}
	~AudioLuaContext() { shutdown(); }

	void init(ProcessingContext* ctx, const std::string& configPath, const std::string& scriptPath);

	void closeProject();

	void addInstance() {}

	void removeInstance(size_t index);

	void duplicateInstance(size_t index);

	void setActive(size_t idx);

	void update(float delta);

	//bool onKey(const iplug::igraphics::IKeyPress& key, bool down);

	void onPadButton(int button, bool down);

	void onMidi(int offset, int status, int data1, int data2);

	void onMidiClock(int button, bool down);

	void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

private:
	void setup();
};