#pragma once

#include <string>
#include <vector>
#include <stack>

#include "IControl.h"
#include "RetroPlug.h"
#include "core/Input.h"

using namespace iplug;
using namespace igraphics;

class RetroPlugView : public IControl {
private:
	rp::RetroPlug* _retroPlug;

	EHost _host;
	IPopupMenu _menu;

public:
	std::function<void(double)> onFrame;

public:
	RetroPlugView(IRECT b, rp::RetroPlug* retroPlug);
	~RetroPlugView();

	void OnInit() override;

	bool IsDirty() override { return true; }

	bool OnKey(const IKeyPress& key, bool down);

	bool OnKey(VirtualKey::Enum key, bool down);

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;

	void OnMouseDown(float x, float y, const IMouseMod& mod);

	void Draw(IGraphics& g) override;

	void OnDrop(const char* str) override;
};
