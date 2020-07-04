#pragma once

#include <iostream>
#include <atomic>
#include "model/ProcessingContext.h"
#include "platform/Menu.h"

namespace sol {
	class state;
};

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
	std::string _configPath;
	std::string _scriptPath;

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

	TimeInfo* _timeInfo = nullptr;

public:
	AudioLuaContext(const std::string& configPath, const std::string& scriptPath);
	~AudioLuaContext() { shutdown(); }

	void init(ProcessingContext* ctx, TimeInfo* timeInfo, double sampleRate);

	void closeProject();

	void addInstance(SystemIndex idx, SameBoyPlugPtr instance, const std::string& componentState);

	void duplicateInstance(SystemIndex sourceIdx, SystemIndex targetIdx, SameBoyPlugPtr instance);

	void removeInstance(SystemIndex index);

	void setActive(SystemIndex idx);

	void update(int frameCount);

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