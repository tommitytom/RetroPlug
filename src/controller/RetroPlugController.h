#pragma once

#include <gainput/gainput.h>
#include "model/UiLuaContext.h"
#include "micromsg/nodemanager.h"
#include "view/RetroPlugRoot.h"
#include "messaging.h"
#include "model/AudioContextProxy.h"
#include "Types.h"
#include "model/ProcessingContext.h"
#include "model/AudioLuaContext.h"
#include "controller/AudioController.h"

#include "IGraphicsStructs.h"

class ChangeListener : public FW::FileWatchListener {
private:
	UiLuaContext* _uiCtx;
	AudioContextProxy* _proxy;

public:
	ChangeListener(UiLuaContext* uiContext = nullptr, AudioContextProxy* proxy = nullptr):
		_uiCtx(uiContext), _proxy(proxy) {}

	~ChangeListener() {}

	void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
		fs::path p(filename);
		if (p.extension() == ".lua") {
			std::cout << "Reloading..." << std::endl;
			if (_uiCtx) {
				_uiCtx->reload();
			}

			if (_proxy) {
				_proxy->reloadLuaContext();
			}
		}
	}
};

class RetroPlugView;

class RetroPlugController {
private:
	UiLuaContext _uiLua;
	AudioContextProxy _proxy;	
	RetroPlugView* _view;
	
	AudioController _audioController;
	iplug::ITimeInfo* _timeInfo;

	FW::FileWatcher _scriptWatcher;
	ChangeListener _listener;

	gainput::InputManager* _padManager;
	gainput::DeviceId _padId;
	bool _padButtons[gainput::PadButtonAxisCount_ + gainput::PadButtonCount_];

	SystemIndex _selected;

	micromsg::NodeManager<NodeTypes> _bus;

public:
	RetroPlugController(iplug::ITimeInfo* timeInfo, double sampleRate);
	~RetroPlugController();

	std::mutex* getMenuLock() { return _audioController.getLock(); }

	void update(float delta);

	void init(iplug::igraphics::IGraphics* graphics, iplug::EHost host);

	//ProcessingContext* processingContext() { return _audioController.getProcessingContext(); }

	AudioController* audioController() { return &_audioController; }

	AudioLuaContextPtr& audioLua() { return _audioController.getLuaContext(); }

	DataBufferPtr saveState() { return _uiLua.saveState(); }

	void loadState(DataBufferPtr buffer) { _uiLua.loadState(buffer); }

private:
	void processPad();
};
