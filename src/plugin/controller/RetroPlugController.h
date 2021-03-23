#pragma once

#ifndef __EMSCRIPTEN__
#include <gainput/gainput.h>
#endif

#include "luawrapper/UiLuaContext.h"
#include "micromsg/nodemanager.h"
#include "view/RetroPlugView.h"
#include "messaging.h"
#include "model/AudioContextProxy.h"
#include "Types.h"
#include "model/ProcessingContext.h"
#include "luawrapper/AudioLuaContext.h"
#include "audio/AudioController.h"
#include "luawrapper/ChangeListener.h"

class RetroPlugController {
private:
	UiLuaContext _uiLua;
	AudioContextProxy _proxy;	
	RetroPlugView* _view;
	
	AudioController _audioController;
	TimeInfo _timeInfo;

	FW::FileWatcher _scriptWatcher;
	ChangeListener _listener;

#ifndef __EMSCRIPTEN__
	gainput::InputManager* _padManager;
	gainput::DeviceId _padId;
	bool _padButtons[gainput::PadButtonAxisCount_ + gainput::PadButtonCount_];
#endif

	SystemIndex _selected;

	micromsg::NodeManager<NodeTypes> _bus;

public:
	RetroPlugController(double sampleRate);
	~RetroPlugController();

	TimeInfo& getTimeInfo() {
		return _timeInfo;
	}

	std::mutex* getMenuLock() { return _audioController.getLock(); }

	void update(double delta);

	void init(iplug::igraphics::IRECT bounds);

	bool onKey(VirtualKey key, bool down) { 
		return _view->OnKey(key, down);
	}

	AudioController* audioController() { return &_audioController; }

	AudioLuaContextPtr& audioLua() { return _audioController.getLuaContext(); }

	DataBufferPtr saveState() { return _uiLua.saveState(); }

	void loadState(DataBufferPtr buffer) { _uiLua.loadState(buffer); }

	RetroPlugView* getView() { return _view; }

private:
	void processPad();
};
