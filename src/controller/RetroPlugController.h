#pragma once

#include <gainput/gainput.h>
#include "model/UiLuaContext.h"
#include "micromsg/nodemanager.h"
#include "view/RetroPlugRoot.h"
#include "messaging.h"
#include "model/RetroPlugProxy.h"
#include "Types.h"
#include "model/ProcessingContext.h"
#include "model/AudioLuaContext.h"

#include "IGraphicsStructs.h"

class ChangeListener : public FW::FileWatchListener {
private:
	UiLuaContext* _uiCtx;
	AudioLuaContext* _audioCtx;

public:
	ChangeListener(UiLuaContext* uiContext = nullptr, AudioLuaContext* audioContext = nullptr): 
		_uiCtx(uiContext), _audioCtx(audioContext) {}

	~ChangeListener() {}

	void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
		fs::path p(filename);
		if (p.extension() == ".lua") {
			std::cout << "Reloading..." << std::endl;
			if (_uiCtx) {
				_uiCtx->reload();
			}

			if (_audioCtx) {
				_audioCtx->reload();
			}
		}
	}
};

class RetroPlugView;

class RetroPlugController {
private:
	RetroPlugProxy _proxy;	
	ProcessingContext _processingContext;
	RetroPlugView* _view;

	UiLuaContext _uiLua;
	AudioLuaContext _audioLua;

	FW::FileWatcher _scriptWatcher;
	ChangeListener _listener;

	gainput::InputManager* _padManager;
	gainput::DeviceId _padId;
	bool _padButtons[gainput::PadButtonAxisCount_ + gainput::PadButtonCount_];

	InstanceIndex _selected;

	micromsg::NodeManager<NodeTypes> _bus;

public:
	RetroPlugController();
	~RetroPlugController();

	void update(float delta);

	void init(iplug::igraphics::IGraphics* graphics, iplug::EHost host);

	ProcessingContext* processingContext() { return &_processingContext; }

	AudioLuaContext* audioLua() { return &_audioLua; }

private:
	void processPad();
};
