#pragma once

#include "FileWatcher/FileWatcher.h"
#include "util/xstring.h"

#include "IGraphicsStructs.h"
#include "model/AudioContextProxy.h"

#include "view/Menu.h"
#include "platform/FileDialog.h"

#include <iostream>

#include <sol/sol.hpp>

//namespace sol { class state; };

class UiLuaContext {
private:
	sol::state* _state;
	std::string _configPath;
	std::string _scriptPath;

	AudioContextProxy* _proxy;

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

	//std::vector<FileDialogFilters> _dialogFilters;
	//DialogType _dialogType = DialogType::None;
	DialogRequest _dialogRequest;

	sol::table _viewRoot;

public:
	UiLuaContext(): _state(nullptr), _proxy(nullptr) {}
	~UiLuaContext() { shutdown(); }

	void init(AudioContextProxy* proxy, const std::string& path, const std::string& scriptPath);

	void update(float delta);

	bool onKey(const iplug::IKeyPress& key, bool down);

	void onDoubleClick(float x, float y, const iplug::igraphics::IMouseMod& mod);

	void onMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod);

	void onDrop(float x, float y, const char* str);

	void onPadButton(int button, bool down);

	void onMenu(std::vector<Menu*>& menus);

	void onMenuResult(int id);

	void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

	bool getDialogRequest(DialogRequest& request);

	void handleDialogCallback(const std::vector<std::string>& paths);

private:
	void setup();
};
