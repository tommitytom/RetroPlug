#pragma once

#include <mutex>

#include "luawrapper/AudioLuaContext.h"
#include "model/ProcessingContext.h"
#include "messaging.h"

using AudioLuaContextPtr = std::shared_ptr<AudioLuaContext>;

class AudioController {
private:
	AudioLuaContextPtr _lua;
	ProcessingContext _processingContext;
	Node* _node = nullptr;
	TimeInfo* _timeInfo;
	std::mutex _lock;
	double _sampleRate;

public:
	AudioController(TimeInfo* timeInfo, double sampleRate): _timeInfo(timeInfo), _sampleRate(sampleRate) {}
	~AudioController() {}

	std::mutex* getLock() { return &_lock; }

	void setNode(Node* node);

	void setAudioSettings(const AudioSettings& settings);

	void fetchState(const FetchStateRequest& req, FetchStateResponse& state);

	bool getSram(SystemIndex idx, DataBuffer<char>* target);

	void onMenu(SystemIndex idx, std::vector<Menu*>& menus);

	void process(float** outputs, size_t frameCount);

	AudioLuaContextPtr& getLuaContext() { return _lua; }
};
