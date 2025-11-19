#include "FrameworkView.h"

#include "IGraphicsFramework.h"

#include "config.h"
#include "foundation/MacroTools.h"

using namespace orb;

using hrc = std::chrono::high_resolution_clock;
using delta_duration = std::chrono::duration<f32>;

FrameworkView::FrameworkView(orb::app::Application& app, orb::app::UiContextPtr uiContext, orb::app::WindowPtr window, ViewCloseFunc&& closeFunc) :
	IControl(IRECT(0.0f, 0.0f, window->getViewManager()->getDimensionsF().w, window->getViewManager()->getDimensionsF().h)), 
	_app(app),
	_lastTime(hrc::now()),
	_closeFunc(std::move(closeFunc)),
	_uiContext(uiContext),
	_window(window),
	_vm(window->getViewManager())
{}

FrameworkView::~FrameworkView() {
	if (_closeFunc) {
		_closeFunc();
	}
}

void FrameworkView::OnInit() {
	
}

bool FrameworkView::OnKeyDown(float x, float y, const IKeyPress& key) {
	return _vm->onKey(orb::KeyEvent{
		.action = KeyAction::Press,
		.key = (VirtualKey)key.VK,
		.down = true
	});
}

bool FrameworkView::OnKeyUp(float x, float y, const IKeyPress& key) {
	return _vm->onKey(orb::KeyEvent{
		.action = KeyAction::Release,
		.key = (VirtualKey)key.VK,
		.down = false
	});
}

MouseButton getMouseButton(const IMouseMod& mod) {
	MouseButton button = MouseButton::Unknown;

	if (mod.L) {
		button = MouseButton::Left;
	} else if (mod.R) {
		button = MouseButton::Right;
	} else if (mod.C) {
		button = MouseButton::Middle;
	}

	return button;
}

void FrameworkView::OnMouseDown(float x, float y, const IMouseMod& mod) {
	//OnMouseOver(x, y, mod);

	orb::Point pos = orb::Point((int32)x, (int32)y);
	MouseButton button = getMouseButton(mod);

	if (button != MouseButton::Unknown) {
		_vm->onMouseButton(orb::MouseButtonEvent{
			.button = button,
			.down = true,
			.position = pos
		});
	}
}

void FrameworkView::OnMouseUp(float x, float y, const IMouseMod& mod) {
	//OnMouseOver(x, y, mod);

	orb::Point pos = orb::Point((int32)x, (int32)y);
	MouseButton button = getMouseButton(mod);

	if (button != MouseButton::Unknown) {
		_vm->onMouseButton(orb::MouseButtonEvent{
			.button = button,
			.down = false,
			.position = pos
		});
	}
}

void FrameworkView::OnMouseDblClick(float x, float y, const IMouseMod& mod) {
	orb::Point pos = orb::Point((int32)x, (int32)y);
	MouseButton button = getMouseButton(mod);

	_vm->onMouseDoubleClick(orb::MouseDoubleClickEvent{
		.button = button,
		.position = pos
	});
}

void FrameworkView::OnMouseWheel(float x, float y, const IMouseMod& mod, float d) {
	_vm->onMouseScroll(orb::MouseScrollEvent{
		.delta = orb::PointF(0.0f, d),
		.position = orb::Point((int32)x, (int32)y)
	});
}

void FrameworkView::OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) {
	orb::Point pos((int32)x, (int32)y);

	if (_mouseOver == false) {
		_vm->onMouseEnter(pos);
		_mouseOver = true;
	}

	_vm->onMouseMove(pos);
}

void FrameworkView::OnMouseOver(float x, float y, const IMouseMod& mod) {
	orb::Point pos((int32)x, (int32)y);

	if (_mouseOver == false) {
		_vm->onMouseEnter(pos);
		_mouseOver = true;
	}

	_vm->onMouseMove(pos);
}

void FrameworkView::OnMouseOut() {
	_vm->onMouseLeave();
	_mouseOver = false;
}

void FrameworkView::OnTouchCancelled(float x, float y, const IMouseMod& mod) {

}

void FrameworkView::OnDrop(const char* str) {
	_vm->onDrop(std::vector<std::string> { str });
}

void FrameworkView::OnRescale() {
	spdlog::info("onRescale");
}

void FrameworkView::OnResize() {
	spdlog::info("onResize, {}, {}", this->GetRECT().W(), this->GetRECT().H());
}

void FrameworkView::Draw(IGraphics& g) {
	hrc::time_point time = hrc::now();
	std::chrono::nanoseconds nanoDelta = time - _lastTime;
	f32 delta = std::chrono::duration_cast<delta_duration>(nanoDelta).count();
	_lastTime = time;

	_app.onUpdate(delta);

	if (_uiContext) {
		_uiContext->runFrame(delta);
	}

	Dimension currentWindow = Dimension((int32)GetRECT().W(), (int32)GetRECT().H());
	
	Dimension dimensions = _vm->getChild(0)->getDimensions();

	if (dimensions != currentWindow) {
		this->SetRECT(IRECT(0.0f, 0.0f, (f32)dimensions.w, (f32)dimensions.h));
		g.Resize(dimensions.w, dimensions.h, 1.0f, true);
	}	

	ECursor cursor = ECursor::ARROW;

	switch (_vm->getShared().cursor) {
		case CursorType::Hand: cursor = ECursor::HAND; break;
		case CursorType::IBeam: cursor = ECursor::IBEAM; break;
		case CursorType::Crosshair: cursor = ECursor::CROSS; break;
		case CursorType::ResizeEW: cursor = ECursor::SIZEWE; break;
		case CursorType::ResizeNS: cursor = ECursor::SIZENS; break;
		case CursorType::ResizeNWSE: cursor = ECursor::SIZENWSE; break;
		case CursorType::NotAllowed: cursor = ECursor::INO; break;
	}

	g.SetMouseCursor(cursor);
}
