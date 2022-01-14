#include "RetroPlugView.h"

RetroPlugView::RetroPlugView(IRECT b, rp::RetroPlug* retroPlug)
	: IControl(b), _retroPlug(retroPlug)
{
}

RetroPlugView::~RetroPlugView() {
	
}

void RetroPlugView::OnInit() {
	
}

void RetroPlugView::OnDrop(const char* str) {
}

bool RetroPlugView::OnKey(const IKeyPress& key, bool down) {
	return OnKey((VirtualKey::Enum)key.VK, down);
}

bool RetroPlugView::OnKey(VirtualKey::Enum key, bool down) {
	return false;
}

void RetroPlugView::OnMouseDblClick(float x, float y, const IMouseMod& mod) {
	
}

void RetroPlugView::OnMouseDown(float x, float y, const IMouseMod& mod) {
	
}

void RetroPlugView::Draw(IGraphics& g) {
	_retroPlug->getUiContext().setNvgContext((NVGcontext*)g.GetDrawContext());
	_retroPlug->getUiContext().processDelta(1 / 60);
}
