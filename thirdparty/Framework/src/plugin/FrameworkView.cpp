#include "FrameworkView.h"

#include "IGraphicsFramework.h"

#include "config.h"
#include "foundation/MacroTools.h"

using namespace fw;

#include INCLUDE_EXAMPLE(EXAMPLE_IMPL)

FrameworkView::FrameworkView(IRECT b, void* nativeWindowHandle) : IControl(b) {
	auto audioManager = std::make_shared<audio::AudioManager>();
	_app = std::make_shared<fw::app::Application>(audioManager);
	_window = _app->setup<EXAMPLE_IMPL>(nativeWindowHandle, fw::Dimension{ (int32)b.W(), (int32)b.H() });
	_vm = _window->getViewManager();
}

void FrameworkView::OnInit() {
	
}

bool FrameworkView::OnKeyDown(float x, float y, const IKeyPress& key) 
{ 
	return _vm->onKey(fw::KeyEvent{
		.key = (VirtualKey::Enum)key.VK,
		.action = KeyAction::Press,
		.down = true
	});
}

bool FrameworkView::OnKeyUp(float x, float y, const IKeyPress& key)
{
	return _vm->onKey(fw::KeyEvent{
		.key = (VirtualKey::Enum)key.VK,
		.action = KeyAction::Release,
		.down = false
	});
}

MouseButton::Enum getMouseButton(const IMouseMod& mod) {
	MouseButton::Enum button = MouseButton::Unknown;

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

	fw::Point pos = fw::Point((int32)x, (int32)y);
	MouseButton::Enum button = getMouseButton(mod);

	if (button != MouseButton::Unknown) {
		_vm->onMouseButton(fw::MouseButtonEvent{
			.button = button,
			.down = true,
			.position = pos
		});
	}
}

void FrameworkView::OnMouseUp(float x, float y, const IMouseMod& mod) {
	//OnMouseOver(x, y, mod);

	fw::Point pos = fw::Point((int32)x, (int32)y);
	MouseButton::Enum button = getMouseButton(mod);

	if (button != MouseButton::Unknown) {
		_vm->onMouseButton(fw::MouseButtonEvent{
			.button = button,
			.down = false,
			.position = pos
		});
	}
}

void FrameworkView::OnMouseDblClick(float x, float y, const IMouseMod& mod) {
	fw::Point pos = fw::Point((int32)x, (int32)y);
	MouseButton::Enum button = getMouseButton(mod);

	_vm->onMouseDoubleClick(fw::MouseDoubleClickEvent{
		.button = button,
		.position = pos
	});
}

void FrameworkView::OnMouseWheel(float x, float y, const IMouseMod& mod, float d) {
	_vm->onMouseScroll(fw::MouseScrollEvent{
		.delta = fw::PointF(0.0f, d),
		.position = fw::Point((int32)x, (int32)y)
	});
}

void FrameworkView::OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) {
	fw::Point pos((int32)x, (int32)y);

	if (_mouseOver == false) {
		_vm->onMouseEnter(pos);
		_mouseOver = true;
	}

	_vm->onMouseMove(pos);
}

void FrameworkView::OnMouseOver(float x, float y, const IMouseMod& mod) {
	fw::Point pos((int32)x, (int32)y);

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
	std::cout << std::endl;
}

void FrameworkView::OnRescale() {
	spdlog::info("onRescale");
}

void FrameworkView::OnResize() {
	spdlog::info("onResize, {}, {}", this->GetRECT().W(), this->GetRECT().H());
}

void FrameworkView::Draw(IGraphics& g) {
	_app->runFrame();

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
