#pragma once

#include "model/AudioLuaContext.h"
#include "model/ProcessingContext.h"

using AudioLuaContextPtr = std::shared_ptr<AudioLuaContext>;

class AudioController {
private:
	AudioLuaContextPtr _lua;
	ProcessingContext _processingContext;
	Node* _node;
	iplug::ITimeInfo* _timeInfo;
	std::mutex _lock;
	double _sampleRate;

public:
	AudioController(iplug::ITimeInfo* timeInfo, double sampleRate): _node(nullptr), _timeInfo(timeInfo), _sampleRate(sampleRate) {}
	~AudioController() {}

	std::mutex* getLock() { return &_lock; }

	void setNode(Node* node);

	void setAudioSettings(const AudioSettings& settings) {
		_processingContext.setAudioSettings(settings);
		_sampleRate = settings.sampleRate;
	}

	//ProcessingContext* getProcessingContext() { return &_processingContext; }

	void onMenu(std::vector<Menu*>& menus) {
		auto ctx = _lua;
		if (ctx) {
			// TODO: This mutex is temporary until I find a good way of sending context menus
			// across threads!
			_lock.lock();
			ctx->onMenu(menus);
			_lock.unlock();
		}
	}

	void process(float** outputs, size_t frameCount) {
		auto ctx = _lua;
		// TODO: This mutex is temporary until I find a good way of sending context menus
		// across threads!
		_lock.lock();
		if (ctx) {
			ctx->update(frameCount);
		}

		_processingContext.process(outputs, (size_t)frameCount);
		_lock.unlock();
	}

	AudioLuaContextPtr& getLuaContext() { return _lua; }
};
