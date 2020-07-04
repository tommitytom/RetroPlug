#pragma once

#include <iostream>
#include <sol/sol.hpp>

#include "model/AudioContextProxy.h"
#include "platform/Menu.h"
#include "platform/FileDialog.h"
#include "platform/ViewWrapper.h"
#include "platform/Keys.h"
#include "util/xstring.h"

class UiLuaContext {
private:
	sol::state* _state;
	std::string _configPath;
	std::string _scriptPath;

	AudioContextProxy* _proxy;
	ViewWrapper _viewWrapper;

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

	sol::table _viewRoot;
	bool _valid = false;

public:
	UiLuaContext(): _state(nullptr), _proxy(nullptr) {}
	~UiLuaContext() { shutdown(); }

	ViewWrapper* getViewWrapper() {
		return &_viewWrapper;
	}

	void init(AudioContextProxy* proxy, const std::string& path, const std::string& scriptPath);

	void update(double delta);

	bool onKey(VirtualKey key, bool down);

	void onDoubleClick(float x, float y);

	void onMouseDown(float x, float y);

	void onDrop(float x, float y, const char* str);

	void onPadButton(int button, bool down);

	void onMenu(std::vector<Menu*>& menus);

	void onMenuResult(int id);

	void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

	void handleDialogCallback(const std::vector<std::string>& paths);

	DataBufferPtr saveState();

	void loadState(DataBufferPtr buffer);

private:
	bool setup();
};
