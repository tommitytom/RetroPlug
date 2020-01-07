#pragma once

#include <gainput/gainput.h>

#include <string>
#include <vector>
#include <stack>
#include "IControl.h"
#include "model/RetroPlug.h"
#include "EmulatorView.h"
#include "ContextMenu.h"
#include "controller/RetroPlugController.h"
#include "util/cxxtimer.hpp"

using namespace iplug;
using namespace igraphics;

using EmulatorViewPtr = std::unique_ptr<EmulatorView>;

class RetroPlugView : public IControl {
private:
	RetroPlugProxy* _proxy;
	std::vector<EmulatorView*> _views;
	//EmulatorView* _active = nullptr;
	InstanceIndex _activeIdx = NO_ACTIVE_INSTANCE;

	IPopupMenu _menu;
	EHost _host;

	LuaContext* _lua;

	cxxtimer::Timer _frameTimer;

public:
	std::function<void(double)> onFrame;

public:
	RetroPlugView(IRECT b, LuaContext* lua, RetroPlugProxy* plug);
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

	void CreatePlugInstance(EmulatorView* view, CreateInstanceType type);

	void SetActive(size_t index);

	IPopupMenu* CreateProjectMenu(bool loaded);

	void NewProject();

	void CloseProject();

	void SaveProject();

	void SaveProjectAs();

	void OpenFindRomDialog();

	void OpenLoadProjectDialog();

	void OpenLoadProjectOrRomDialog();
	
	void RemoveActive();

	/*int GetViewIndex(EmulatorView* view) {
		for (int i = 0; i < _views.size(); i++) {
			if (_views[i] == view) {
				return i;
			}
		}

		return -1;
	}*/

	void SelectActiveAtPoint(float x, float y) {
		for (auto& view : _views) {
			if (view->GetArea().Contains(x, y)) {
				SetActive(view->getIndex());
				break;
			}
		}
	}

	void UpdateActive();
};
