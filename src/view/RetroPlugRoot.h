#pragma once

#include <gainput/gainput.h>

#include <string>
#include <vector>
#include <stack>
#include "IControl.h"
#include "EmulatorView.h"
#include "ContextMenu.h"
#include "controller/RetroPlugController.h"
#include "util/cxxtimer.hpp"
#include "platform/FileDialog.h"

using namespace iplug;
using namespace igraphics;

using EmulatorViewPtr = std::unique_ptr<EmulatorView>;

const FileDialogFilters GAMEBOY_ROM_FILTER = { TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") };
const FileDialogFilters GAMEBOY_SAV_FILTER = { TSTR("GameBoy Saves"), TSTR("*.sav") };
const FileDialogFilters RETROPLUG_PROJECT_FILTER = { TSTR("RetroPlug Projects"), TSTR("*.retroplug") };
const FileDialogFilters ALL_SUPPORTED_FILTER = { TSTR("All Supported Types"), TSTR("*.gb;*.gbc;*.retroplug") };

class RetroPlugView : public IControl {
private:
	RetroPlugProxy* _proxy;
	std::vector<EmulatorView*> _views;
	InstanceIndex _activeIdx = NO_ACTIVE_INSTANCE;

	IPopupMenu _menu;
	EHost _host;

	UiLuaContext* _lua;

	cxxtimer::Timer _frameTimer;

	int _syncMode = 1;
	bool _autoPlay = true;

public:
	std::function<void(double)> onFrame;

public:
	RetroPlugView(IRECT b, UiLuaContext* lua, RetroPlugProxy* plug);
	~RetroPlugView();

	EmulatorView* GetActiveView() {
		InstanceIndex idx = _proxy->activeIdx();
		if (idx != NO_ACTIVE_INSTANCE) {
			return _views[idx];
		}
		
		return nullptr;
	}

	void OnInit() override;

	bool IsDirty() override { return true; }

	bool OnKey(const IKeyPress& key, bool down);

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;

	void OnMouseDown(float x, float y, const IMouseMod& mod);

	void Draw(IGraphics& g) override;

	void OnDrop(float x, float y, const char* str) override;

	void LoadProject(const tstring& path);

	void LoadProjectOrRom(const tstring& path);

private:
	void UpdateLayout();

	void SetActive(size_t index);

	void NewProject();

	void CloseProject();

	void SaveProject();

	void SaveProjectAs();

	void OpenFindRomDialog();

	void OpenLoadProjectDialog();

	void OpenLoadProjectOrRomDialog();

	void OpenLoadRomDialog(InstanceIndex idx, GameboyModel model) {
		std::vector<tstring> paths = BasicFileOpen(GetUI(), { GAMEBOY_ROM_FILTER }, false);
		if (paths.size() > 0) {
			_lua->loadRom(_activeIdx, ws2s(paths[0]), model);
		}
	}

	void OpenLoadSavDialog() {
		std::vector<tstring> paths = BasicFileOpen(GetUI(), { GAMEBOY_SAV_FILTER }, false);
		if (paths.size() > 0) {
			_lua->loadSram(_activeIdx, ws2s(paths[0]), true);
		}
	}

	void OpenSaveSavDialog() {
		tstring path = BasicFileSave(GetUI(), { GAMEBOY_SAV_FILTER });
		if (path.size() > 0) {
			_lua->saveSram(_activeIdx, ws2s(path));
		}
	}
	
	void RemoveActive();

	void RequestSave();

	void SelectActiveAtPoint(float x, float y) {
		for (auto& view : _views) {
			if (view->GetArea().Contains(x, y)) {
				SetActive(view->GetIndex());
				break;
			}
		}
	}

	void UpdateActive();
};
