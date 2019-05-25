#include "RetroPlugRoot.h"

#include <cmath>

RetroPlugRoot::RetroPlugRoot(IRECT b, RetroPlug* plug): IControl(b), _plug(plug) {

}

void RetroPlugRoot::OnInit() {
	if (!_active) {
		SameBoyPlugPtr plug = _plug->getPlug(0);
		if (!plug) {
			plug = _plug->addInstance(EmulatorType::SameBoy);
		}
		
		IRECT b(_views.size() * 320, 0, _views.size() * 320, 288);
		EmulatorView* view = new EmulatorView(b, plug);
		AddView(view);
	}
}

bool RetroPlugRoot::OnKey(const IKeyPress& key, bool down) {
	if (_active) {
		if (key.VK == VirtualKeys::Tab && down) {
			_activeIdx = (_activeIdx + 1) % _views.size();
			SetActive(_views[_activeIdx]);
			return true;
		}

		return _active->OnKey(key, down);
	}

	return false;
}

void RetroPlugRoot::OnMouseDown(float x, float y, const IMouseMod& mod) {
	for (auto view : _views) {
		if (view->GetRECT().Contains(x, y)) {
			view->OnMouseDown(x, y, mod);
			break;
		}
	}
}

void RetroPlugRoot::DuplicatePlug(EmulatorView* view) {
	SameBoyPlugPtr source = view->Plug();
	SameBoyPlugPtr target = _plug->addInstance(EmulatorType::SameBoy);
	
	target->init(source->romPath());

	size_t stateSize = source->saveStateSize();
	char* buf = new char[stateSize];
	source->saveState(buf, stateSize);
	target->loadState(buf, stateSize);

	delete[] buf;

	source->setLinkTarget(target.get());
	target->setLinkTarget(source.get());

	IRECT b(_views.size() * 320, 0, _views.size() * 320 + 320, 288);
	EmulatorView* targetView = new EmulatorView(b, target);
	AddView(targetView);
}

void RetroPlugRoot::AddView(EmulatorView* view) {
	auto w = (std::max((int)_views.size(), 1) + 1) * 320;

	GetUI()->Resize(w, 288, 1);
	SetRECT(IRECT(0, 0, w, 288));
	GetUI()->AttachControl(view);
	view->SetRECT(IRECT(_views.size() * 320, 0, _views.size() * 320 + 320, 288));

	view->OnDuplicateRequest([this](EmulatorView * view) { DuplicatePlug(view); });
	_views.push_back(view);

	SetActive(view);
	_activeIdx = _views.size() - 1;
}

void RetroPlugRoot::SetActive(EmulatorView* view) {
	if (_active) {
		_active->SetAlpha(0.75f);
	}

	_active = view;
	_active->SetAlpha(1.0f);
}
