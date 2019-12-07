#include "RetroPlugController.h"

RetroPlugBinder::RetroPlugBinder(RetroPlug* model): _model(model), _listener(&_lua) {
	memset(_padButtons, 0, sizeof(_padButtons));
	_padManager = new gainput::InputManager();
	_padId = _padManager->CreateDevice<gainput::InputDevicePad>();

	const std::string path = "../src/scripts";
	_lua.init(_model, path);
	_scriptWatcher.addWatch(path, &_listener, true);
}

RetroPlugBinder::~RetroPlugBinder() {
	delete _padManager;
}

void RetroPlugBinder::update(float delta) {
	_padManager->Update();

	for (int i = 0; i < gainput::PadButtonCount_; ++i) {
		bool down = _padManager->GetDevice(_padId)->GetBool(gainput::PadButtonStart + i);
		if (_padButtons[i] != down) {
			_padButtons[i] = down;
			onPadButton(i + gainput::PadButtonStart, down);
		}
	}
}

bool RetroPlugBinder::onKey(const iplug::igraphics::IKeyPress& key, bool down) {
	return _lua.onKey(key, down);
}

void RetroPlugBinder::onPadButton(int button, bool down) {
	_lua.onPadButton(button, down);
}
