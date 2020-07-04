#include "RetroPlugController.h"
#include "config.h"
#include "platform/Path.h"
#include "platform/Resource.h"
#include "resource.h"

#include "luawrapper/ConfigScripts.h"
#include "luawrapper/ConfigScriptWriter.h"

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

RetroPlugController::RetroPlugController(double sampleRate)
	: _listener(&_uiLua, &_proxy), _audioController(&_timeInfo, sampleRate), _proxy(&_audioController)
{
	_bus.addCall<calls::LoadRom>(4);
	_bus.addCall<calls::SwapSystem>(4);
	_bus.addCall<calls::TakeSystem>(4);
	_bus.addCall<calls::DuplicateSystem>(1);
	_bus.addCall<calls::ResetSystem>(4);
	_bus.addCall<calls::TransmitVideo>(16);
	_bus.addCall<calls::UpdateProjectSettings>(4);
	_bus.addCall<calls::UpdateSystemSettings>(4);
	_bus.addCall<calls::PressButtons>(32);
	_bus.addCall<calls::FetchState>(1);
	_bus.addCall<calls::ContextMenuResult>(1);
	_bus.addCall<calls::SwapLuaContext>(4);
	_bus.addCall<calls::SetActive>(4);
	_bus.addCall<calls::SetRom>(4);
	_bus.addCall<calls::SetSram>(4);
	_bus.addCall<calls::SetState>(4);
	_bus.addCall<calls::EnableRendering>(1);

	_proxy.setNode(_bus.createNode(NodeTypes::Ui, { NodeTypes::Audio }));
	_audioController.setNode(_bus.createNode(NodeTypes::Audio, { NodeTypes::Ui }));

	_bus.start();

	memset(_padButtons, 0, sizeof(_padButtons));
	_padManager = new gainput::InputManager();
	_padId = _padManager->CreateDevice<gainput::InputDevicePad>();

	// Make sure the config script exists
	fs::path configDir = getContentPath(fs::path(PLUG_VERSION_STR));
	ConfigScriptWriter::write(configDir);

	fs::path scriptPath = fs::path(__FILE__).parent_path().parent_path().parent_path() / "retroplug" / "scripts";
	_uiLua.init(&_proxy, configDir.string(), scriptPath.string());

	_proxy.setScriptDirs(configDir.string(), scriptPath.string());

	if (fs::exists(scriptPath)) {
		_scriptWatcher.addWatch(scriptPath.string(), &_listener, true);
	}

	_scriptWatcher.addWatch(configDir.string(), &_listener, true);
}

RetroPlugController::~RetroPlugController() {
	delete _padManager;
}

void RetroPlugController::update(double delta) {
	processPad();
	_scriptWatcher.update();
}

void RetroPlugController::init(iplug::igraphics::IRECT bounds) {
	_view = new RetroPlugView(bounds, &_uiLua, &_proxy, &_audioController);

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
					_uiLua.onPadButton(r, false);
				}

				_padButtons[l] = true;
				_uiLua.onPadButton(l, true);
			}
		} else if (val > AXIS_BUTTON_THRESHOLD) {
			if (_padButtons[r] == false) {
				if (_padButtons[l] == true) {
					_padButtons[l] = false;
					_uiLua.onPadButton(l, false);
				}

				_padButtons[r] = true;
				_uiLua.onPadButton(r, true);
			}
		} else {
			if (_padButtons[l] == true) {
				_padButtons[l] = false;
				_uiLua.onPadButton(l, false);
			}

			if (_padButtons[r] == true) {
				_padButtons[r] = false;
				_uiLua.onPadButton(r, false);
			}
		}
	}

	for (int i = 0; i < gainput::PadButtonCount_; ++i) {
		int idx = gainput::PadButtonStart + i;
		bool down = _padManager->GetDevice(_padId)->GetBool(idx);
		if (_padButtons[idx] != down) {
			_padButtons[idx] = down;
			_uiLua.onPadButton(idx, down);
		}
	}
}
