#pragma once

#include "FileWatcher/FileWatcher.h"
#include "util/xstring.h"

#include "IGraphicsStructs.h"
#include "model/RetroPlugProxy.h"

#include "view/Menu.h"
#include "platform/FileDialog.h"

#include <iostream>

namespace sol { class state; };

class UiLuaContext {
private:
	sol::state* _state;
	std::string _configPath;
	std::string _scriptPath;

	RetroPlugProxy* _proxy;

	bool _haltFrameProcessing = false;
	std::atomic_bool _reload = false;

	std::vector<FileDialogFilters> _dialogFilters;
	DialogType _dialogType = DialogType::None;

public:
	UiLuaContext(): _state(nullptr), _proxy(nullptr) {}
	~UiLuaContext() { shutdown(); }

	void init(RetroPlugProxy* proxy, const std::string& path, const std::string& scriptPath);

	void loadRom(InstanceIndex idx, const std::string& path, GameboyModel model);
	
	void closeProject();

	void loadProject(const std::string& path);

	void saveProject(const FetchStateResponse& res);

	void removeInstance(InstanceIndex index);

	void duplicateInstance(InstanceIndex index);

	void setActive(InstanceIndex idx);

	void resetInstance(InstanceIndex idx, GameboyModel model);

	void newSram(InstanceIndex idx);
	
	void saveSram(InstanceIndex idx, const std::string& path);

	void loadSram(InstanceIndex idx, const std::string& path, bool reset);

	void update(float delta);

	bool onKey(const iplug::IKeyPress& key, bool down);

	void onPadButton(int button, bool down);

	void onDrop(const char* str);

	void onMenu(std::vector<Menu*>& menus);

	void onMenuResult(int id);

	void reload();

	void scheduleReload() { _reload = true; }

	void shutdown();

	DialogType getDialogFilters(std::vector<FileDialogFilters>& filters);

	void handleDialogCallback(const std::vector<std::string>& paths);

private:
	void setup();
};
