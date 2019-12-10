#pragma once

#include <gainput/gainput.h>
#include "model/RetroPlug.h"
#include "model/LuaContext.h"

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

using InstanceIndex = size_t;

class RetroPlugBinder {
private:
	RetroPlug* _model;

	LuaContext _lua;
	FW::FileWatcher _scriptWatcher;
	ChangeListener _listener;

	gainput::InputManager* _padManager;
	gainput::DeviceId _padId;
	bool _padButtons[gainput::PadButtonAxisCount_ + gainput::PadButtonCount_];

	InstanceIndex _selected;

public:
	RetroPlugBinder(RetroPlug* model);
	~RetroPlugBinder();

	void update(float delta);

	LuaContext& lua() { return _lua; }

	bool onKey(const iplug::igraphics::IKeyPress& key, bool down);

	void onPadButton(int button, bool down);

	void generateMenu() {}
};
