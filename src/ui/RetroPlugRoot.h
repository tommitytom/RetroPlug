#pragma once

#include <string>
#include <vector>
#include <stack>
#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "EmulatorView.h"

enum class RetroPlugLayout {
	Auto,
	Row,
	Column,
	Grid
};

enum class CreateInstanceType : int {
	LoadRom,
	SameRom,
	Duplicate
};

enum class ProjectMenuItems : int {
	New,
	Load,
	Save,
	SaveAs,

	Sep1,

	SaveOptions,

	Sep2,

	AddInstance,
	RemoveInstance,
	Layout
};

enum class SaveModes {
	SaveSram,
	SaveState
};

class RetroPlugRoot : public IControl {
private:
	RetroPlug* _plug;
	std::vector<EmulatorView*> _views;
	EmulatorView* _active = nullptr;
	size_t _activeIdx = 0;
	RetroPlugLayout _layout = RetroPlugLayout::Auto;
	SaveModes _saveMode = SaveModes::SaveSram;

public:
	RetroPlugRoot(IRECT b, RetroPlug* plug);
	~RetroPlugRoot() {}

	void OnInit() override;

	bool OnKey(const IKeyPress& key, bool down);

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override {
		_active->OnMouseDblClick(x, y, mod);
	}

	void OnMouseDown(float x, float y, const IMouseMod& mod);

	void Draw(IGraphics& g) override {}

private:
	void UpdateLayout();

	void CreatePlugInstance(EmulatorView* view, CreateInstanceType type);

	EmulatorView* AddView(SameBoyPlugPtr plug);

	void SetActive(EmulatorView* view);

	void CreateProjectMenu(IPopupMenu* target, bool loaded);

	void NewProject();

	void CloseProject();

	void SaveProject();

	void SaveProjectAs();

	void LoadProject();
	
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
