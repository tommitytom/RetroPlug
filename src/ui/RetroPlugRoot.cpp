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
		
		IRECT b(0, 0, 320, 288);
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

void RetroPlugRoot::CreatePlugInstance(EmulatorView* view, CreateInstanceType type) {
	SameBoyPlugPtr target = _plug->addInstance(EmulatorType::SameBoy);
	SameBoyPlugPtr source = view->Plug();
	if (type != CreateInstanceType::LoadRom) {
		target->init(source->romPath());

		if (type == CreateInstanceType::Duplicate) {
			size_t stateSize = source->saveStateSize();
			char* buf = new char[stateSize];
			source->saveState(buf, stateSize);
			target->loadState(buf, stateSize);
			delete[] buf;
		}
	}	

	if (target->active()) {
		source->setLinkTarget(target.get());
		target->setLinkTarget(source.get());
	}

	IRECT b(_views.size() * 320, 0, _views.size() * 320 + 320, 288);
	EmulatorView* targetView = new EmulatorView(b, target);
	AddView(targetView);
}

void RetroPlugRoot::AddView(EmulatorView* view) {
	auto w = (_views.size() + 1) * 320;
	IRECT b(0, 0, w, 288);

	GetUI()->SetSizeConstraints(320, b.W(), 288, 288);
	GetUI()->Resize(b.W(), b.H(), 1);
	GetUI()->GetControl(0)->SetRECT(b);
	SetRECT(b);

	int x = _views.size() * 320;
	view->SetRECT(IRECT(x, 0, x + 320, 288));
	GetUI()->AttachControl(view);

	view->OnDuplicateRequest([this](EmulatorView* view, CreateInstanceType type) { CreatePlugInstance(view, type); });
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
