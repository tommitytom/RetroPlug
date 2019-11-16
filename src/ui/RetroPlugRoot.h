#pragma once

#include <gainput/gainput.h>

#include <string>
#include <vector>
#include <stack>
#include "IControl.h"
#include "RetroPlug.h"
#include "EmulatorView.h"
#include "ContextMenu.h"

using namespace iplug;
using namespace igraphics;

class RetroPlugRoot : public IControl {
private:
	RetroPlug* _plug;
	std::vector<EmulatorView*> _views;
	EmulatorView* _active = nullptr;
	size_t _activeIdx = 0;

	IPopupMenu _menu;
	EHost _host;

	gainput::InputManager* _padManager;
	gainput::DeviceId _padId;
	bool _padButtons[gainput::PadButtonCount_];

public:
	RetroPlugRoot(IRECT b, RetroPlug* plug, EHost host);
	~RetroPlugRoot();

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

	EmulatorView* AddView(SameBoyPlugPtr plug);

	void SetActive(EmulatorView* view);

	IPopupMenu* CreateProjectMenu(bool loaded);

	void NewProject();

	void CloseProject();

	void SaveProject();

	void SaveProjectAs();

	void OpenFindRomDialog();

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

	void SelectActiveAtPoint(float x, float y) {
		for (auto view : _views) {
			if (view->GetArea().Contains(x, y)) {
				_activeIdx = GetViewIndex(view);
				SetActive(_views[_activeIdx]);
				break;
			}
		}
	}
};
