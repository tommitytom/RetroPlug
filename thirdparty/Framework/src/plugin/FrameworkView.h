#pragma once

#include "IControl.h"

#include "application/Application.h"
#include "application/Window.h"

#include "graphics/Canvas.h"

#include "ui/PanelView.h"

using namespace iplug;
using namespace igraphics;

class FrameworkView : public IControl {
private:
	fw::app::UiContext& _uiContext;
	fw::app::WindowPtr _window;
	fw::ViewManagerPtr _vm;
	bool _mouseOver = false;

public:
	FrameworkView(fw::app::UiContext& uiContext, fw::app::WindowPtr window);
	~FrameworkView() {}

	void OnInit() override;

	bool OnKeyDown(float x, float y, const IKeyPress& key) override;

	bool OnKeyUp(float x, float y, const IKeyPress& key) override;

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;

	void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override;

	void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override;

	void OnMouseDown(float x, float y, const IMouseMod& mod) override;

	void OnMouseUp(float x, float y, const IMouseMod& mod) override;

	void OnMouseOver(float x, float y, const IMouseMod& mod) override;

	void OnMouseOut() override;

	void OnTouchCancelled(float x, float y, const IMouseMod& mod) override;

	void OnDrop(const char* str) override;

	void OnRescale() override;

	void OnResize() override;

	void Draw(IGraphics& g) override;

	bool IsDirty() override { return true; }
};
