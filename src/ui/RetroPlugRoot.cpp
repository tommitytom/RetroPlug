#include "RetroPlugRoot.h"

RetroPlugRoot::RetroPlugRoot(IRECT b, RetroPlug* plug): IControl(b), _plug(plug) {

}

void RetroPlugRoot::OnInit() {
	if (!_active) {
		SameBoyPlugPtr plug = _plug->getPlug(0);
		if (!plug) {
			plug = _plug->addInstance(EmulatorType::SameBoy);
		}
		
		IRECT b(_views.size() * 100, 0, 320, 288);
		EmulatorView* view = new EmulatorView(b, plug);
		AddView(view);
	}
}

bool RetroPlugRoot::OnKey(const IKeyPress& key, bool down) {
	if (_active) {
		if (key.VK == VirtualKeys::Num1) {
			DuplicatePlug(_active);
			return true;
		}

		return _active->OnKey(key, down);
	}

	return false;
}

void RetroPlugRoot::AddView(EmulatorView* view) {
	GetUI()->Resize((_views.size() + 1) * 320, 288, 1);
	GetUI()->AttachControl(view);
	view->SetRECT(IRECT(_views.size() * 320, 0, _views.size() * 320 + 320, 288));

	//SetRECT(IRECT(0, 0, (_views.size() + 1) * 320, 288));
	
	view->OnDuplicateRequest([this](EmulatorView* view) { DuplicatePlug(view); });

	_active = view;
	_views.push_back(view);
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

	IRECT b(_views.size() * 100, 0, 320, 288);
	EmulatorView* targetView = new EmulatorView(b, target);
	AddView(targetView);
}
