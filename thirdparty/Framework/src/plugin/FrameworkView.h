#pragma once

#include "IControl.h"

#include "application/Application.h"
#include "application/Window.h"

#include "graphics/Canvas.h"

using namespace iplug;
using namespace igraphics;

#include "ui/PanelView.h"

#include <iostream>

class TestView : public fw::View {
private:
	fw::Color4F _col = fw::Color4F::red;

public:
	TestView() {
		setType<TestView>();
		setSizingPolicy(fw::SizingPolicy::FitToParent);
	}

	void onInitialize() override {
		addChild<fw::PanelView>("Panel A")->setArea({ 0, 0, 20, 20 });
		addChild<fw::PanelView>("Panel B")->setArea({ 20, 20, 20, 20 });
	}

	void onResize(const fw::ResizeEvent& ev) override {
		std::cout << "" << std::endl;
	}

	void onRender(fw::engine::Canvas& canvas) override {
		canvas.fillRect(getDimensions(), _col);
	}

	bool onMouseButton(const fw::MouseButtonEvent& ev) override {
		if (_col == fw::Color4F::red) {
			_col = fw::Color4F::blue;
		} else {
			_col = fw::Color4F::red;
		}

		return true;
	}
};

class FrameworkView : public IControl {
private:
	fw::app::Application _app;
	fw::app::WindowPtr _window;

	bool _mouseOver = false;

public:
	FrameworkView(IRECT b, void* nativeWindowHandle);
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
