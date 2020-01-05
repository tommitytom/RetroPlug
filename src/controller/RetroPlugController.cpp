#include "RetroPlugController.h"
#include "config.h"

namespace AxisButtons {
	enum AxisButton {
		LeftStickLeft = 0,
		LeftStickRight = 1,
		LeftStickDown = 2,
		LeftStickUp = 3,
		RightStickLeft = 4,
		RightStickRight = 5,
		RightStickDown = 6,
		RightStickUp = 7,
		COUNT
	};
}
using AxisButtons::AxisButton;

const float AXIS_BUTTON_THRESHOLD = 0.5f;

RetroPlugController::RetroPlugController(): _listener(&_lua) {
	_bus.addCall<calls::LoadRom>(4);
	_bus.addCall<calls::SwapInstance>(4);
	_bus.addCall<calls::TakeInstance>(4);
	_bus.addCall<calls::TransmitVideo>();
	_bus.addCall<calls::UpdateSettings>(4);
	_bus.addCall<calls::PressButtons>(32);

	_proxy.setNode(_bus.createNode(NodeTypes::Ui, { NodeTypes::Audio }));
	_processingContext.setNode(_bus.createNode(NodeTypes::Audio, { NodeTypes::Ui }));

	_bus.start();

	memset(_padButtons, 0, sizeof(_padButtons));
	_padManager = new gainput::InputManager();
	_padId = _padManager->CreateDevice<gainput::InputDevicePad>();

	const std::string path = "../src/scripts";
	_lua.init(&_model, &_proxy, path);
	_scriptWatcher.addWatch(path, &_listener, true);
}

RetroPlugController::~RetroPlugController() {
	delete _padManager;
}

void RetroPlugController::update(float delta) {
	processPad();
	_scriptWatcher.update();
}

void RetroPlugController::init(iplug::igraphics::IGraphics* graphics, iplug::EHost host) {
	//pGraphics->AttachCornerResizer(kUIResizerScale, false);
	graphics->AttachPanelBackground(COLOR_BLACK);
	graphics->HandleMouseOver(true);
	graphics->LoadFont("Roboto-Regular", GAMEBOY_FN);
	graphics->LoadFont("Early-Gameboy", GAMEBOY_FN);

	graphics->SetKeyHandlerFunc([&](const IKeyPress& key, bool isUp) {
		return _lua.onKey(key, !isUp);
	});

	_view = new RetroPlugView(graphics->GetBounds(), &_lua, &_proxy);
	graphics->AttachControl(_view);

	_view->onFrame = [&](double delta) {
		update(delta);
	};
}

void RetroPlugController::processPad() {
	_padManager->Update();

	for (int i = 0; i < AxisButtons::COUNT / 2; ++i) {
		float val = _padManager->GetDevice(_padId)->GetFloat(i);
		int l = i * 2;
		int r = i * 2 + 1;

		if (val < -AXIS_BUTTON_THRESHOLD) {
			if (_padButtons[l] == false) {
				if (_padButtons[r] == true) {
					_padButtons[r] = false;
					_lua.onPadButton(r, false);
				}

				_padButtons[l] = true;
				_lua.onPadButton(l, true);
			}
		} else if (val > AXIS_BUTTON_THRESHOLD) {
			if (_padButtons[r] == false) {
				if (_padButtons[l] == true) {
					_padButtons[l] = false;
					_lua.onPadButton(l, false);
				}

				_padButtons[r] = true;
				_lua.onPadButton(r, true);
			}
		} else {
			if (_padButtons[l] == true) {
				_padButtons[l] = false;
				_lua.onPadButton(l, false);
			}

			if (_padButtons[r] == true) {
				_padButtons[r] = false;
				_lua.onPadButton(r, false);
			}
		}
	}

	for (int i = 0; i < gainput::PadButtonCount_; ++i) {
		int idx = gainput::PadButtonStart + i;
		bool down = _padManager->GetDevice(_padId)->GetBool(idx);
		if (_padButtons[idx] != down) {
			_padButtons[idx] = down;
			_lua.onPadButton(idx, down);
		}
	}
}
