#pragma once

#include "IControl.h"

#include "application/Application.h"
#include "application/Window.h"

#include "graphics/Canvas.h"

#include "ui/PanelView.h"

using namespace iplug;
using namespace igraphics;

using ViewCloseFunc = std::function<void()>;

class FrameworkView : public IControl {
private:
	orb::app::UiContextPtr _uiContext;
	orb::app::WindowPtr _window;
	orb::ViewManagerPtr _vm;
	bool _mouseOver = false;
	orb::app::Application& _app;
	std::chrono::high_resolution_clock::time_point _lastTime;
	ViewCloseFunc _closeFunc;

public:
	FrameworkView(orb::app::Application& app, orb::app::UiContextPtr uiContext, orb::app::WindowPtr window, ViewCloseFunc&& closeFunc);
	~FrameworkView();

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
