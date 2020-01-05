#pragma once

#include <gainput/gainput.h>
#include "model/RetroPlug.h"
#include "model/LuaContext.h"
#include "micromsg/nodemanager.h"
#include "view/RetroPlugRoot.h"
#include "messaging.h"
#include "model/RetroPlugProxy.h"
#include "Types.h"
#include "model/ProcessingContext.h"

#include "IGraphicsStructs.h"

class ChangeListener : public FW::FileWatchListener {
private:
	LuaContext* _context;

public:
	ChangeListener(LuaContext* context = nullptr): _context(context) {}
	~ChangeListener() {}

	void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
		std::cout << "Reloading..." << std::endl;

		if (_context) {
			_context->reload();
		}
	}
};

class RetroPlugView;

class RetroPlugController {
private:
	RetroPlug _model;
	RetroPlugProxy _proxy;	
	ProcessingContext _processingContext;
	RetroPlugView* _view;

	LuaContext _lua;
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

private:
	void processPad();
};
