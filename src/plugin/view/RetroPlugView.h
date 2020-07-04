#pragma once

#include <gainput/gainput.h>

#include <string>
#include <vector>
#include <stack>
#include "IControl.h"
#include "SystemView.h"
#include "controller/RetroPlugController.h"
#include "util/cxxtimer.hpp"
#include "platform/FileDialog.h"
#include "controller/AudioController.h"
#include "platform/ViewWrapper.h"

using namespace iplug;
using namespace igraphics;

using SystemViewPtr = std::unique_ptr<SystemView>;

class RetroPlugView : public IControl {
private:
	AudioContextProxy* _proxy;
	std::vector<SystemView*> _views;
	SystemIndex _activeIdx = NO_ACTIVE_SYSTEM;

	EHost _host;
	IPopupMenu _menu;

	UiLuaContext* _lua;
	AudioController* _audioController;

	cxxtimer::Timer _frameTimer;
	double _timeSinceVideo = 0.0;

	int _syncMode = 1;
	bool _autoPlay = true;

public:
	std::function<void(double)> onFrame;

public:
	RetroPlugView(IRECT b, UiLuaContext* lua, AudioContextProxy* proxy, AudioController* audioController);
	~RetroPlugView();

	SystemView* GetActiveView() {
		SystemIndex idx = _proxy->getProject()->selectedSystem;
		if (idx != NO_ACTIVE_SYSTEM) {
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

private:
	void UpdateLayout();

	void UpdateSelected();

	void ProcessDialog();
};
