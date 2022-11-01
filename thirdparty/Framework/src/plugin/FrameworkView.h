#pragma once

#include "IControl.h"

using namespace iplug;
using namespace igraphics;

class FrameworkView : public IControl {
public:
	FrameworkView(IRECT b) : IControl(b) {}
	~FrameworkView() {}

	void OnInit() override {}

	bool IsDirty() override { return true; }

	bool OnKeyDown(float x, float y, const IKeyPress& key) override { return false; }

	bool OnKeyUp(float x, float y, const IKeyPress& key) override { return false; }

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override {}

	void OnMouseDown(float x, float y, const IMouseMod& mod) override {}

	void Draw(IGraphics& g) override {}

	//void OnDrop(float x, float y, const char* str) override {}
};
