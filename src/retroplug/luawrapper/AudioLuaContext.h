#pragma once

#include <iostream>
#include <atomic>
#include <sol/sol.hpp>
#include "model/ProcessingContext.h"
#include "platform/Menu.h"

struct TimeInfo
{
	double mTempo = 120.0;
	double mSamplePos = -1.0;
	double mPPQPos = -1.0;
	double mLastBar = -1.0;
	double mCycleStart = -1.0;
	double mCycleEnd = -1.0;

	int mNumerator = 4;
	int mDenominator = 4;

	bool mTransportIsRunning = false;
	bool mTransportLoopEnabled = false;
};

class AudioLuaContext {
private:
	sol::state* _state = nullptr;
	sol::table _controller;
	std::string _configPath;
	std::string _scriptPath;

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

	ProcessingContext* _context;
	TimeInfo* _timeInfo = nullptr;
	double _sampleRate = 44100;
	
	bool _valid = false;

public:
	AudioLuaContext(const std::string& configPath, const std::string& scriptPath);
	~AudioLuaContext() { shutdown(); }

	void init(ProcessingContext* ctx, TimeInfo* timeInfo, double sampleRate);

	void closeProject();

	void addSystem(SystemIndex idx, SameBoyPlugPtr system, const std::string& componentState);

	void duplicateSystem(SystemIndex sourceIdx, SystemIndex targetIdx, SameBoyPlugPtr system);

	void removeSystem(SystemIndex index);

	void setActive(SystemIndex idx);

	void setSampleRate(double sampleRate);

	void update(int frameCount);

	void onMidi(int offset, int status, int data1, int data2);

	void onMidiClock(int button, bool down);

	void onMenu(SystemIndex idx, std::vector<Menu*>& menus);

	void onMenuResult(int id);

	//void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

	std::string serializeSystem(SystemIndex index);

	std::string serializeSystems();

	void deserializeSystems(const std::string& data);

	void deserializeComponents(const std::array<std::string, 4>& components);

private:
	bool setup();
};