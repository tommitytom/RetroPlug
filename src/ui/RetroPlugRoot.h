#pragma once

#include <string>
#include <vector>
#include <stack>
#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "EmulatorView.h"
#include "ContextMenu.h"

class RetroPlugRoot : public IControl {
private:
	RetroPlug* _plug;
	std::vector<EmulatorView*> _views;
	EmulatorView* _active = nullptr;
	size_t _activeIdx = 0;

	IPopupMenu _menu;

public:
	RetroPlugRoot(IRECT b, RetroPlug* plug);
	~RetroPlugRoot();

	void OnInit() override;

	bool IsDirty() override { return true; }

	bool OnKey(const IKeyPress& key, bool down);

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override {
		if (_active) {
			_active->OpenLoadRomDialog(GameboyModel::Auto);
		}
	}

	void OnMouseDown(float x, float y, const IMouseMod& mod);

	void Draw(IGraphics& g) override;

private:
	bool IsActive() {
		return _views.size() > 0 && _views[0]->Plug() && _views[0]->Plug()->active();
	}

	void UpdateLayout();

	void CreatePlugInstance(EmulatorView* view, CreateInstanceType type);

	EmulatorView* AddView(SameBoyPlugPtr plug);

	void SetActive(EmulatorView* view);

	IPopupMenu* CreateProjectMenu(bool loaded);

	void OnPopupMenuSelection(IPopupMenu* selectedMenu, int valIdx);

	void NewProject();

	void CloseProject();

	void SaveProject();

	void SaveProjectAs();

	void OpenLoadProjectDialog();

	void OpenLoadProjectOrRomDialog();
	
	void RemoveActive();

	int GetViewIndex(EmulatorView* view) {
		for (int i = 0; i < _views.size(); i++) {
			if (_views[i] == view) {
				return i;
			}
		}

		return -1;
	}
};
