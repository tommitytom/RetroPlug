#include "EmulatorView.h"

#include "platform/FileDialog.h"
#include "platform/Path.h"

#include <tao/json.hpp>

EmulatorView::EmulatorView(IRECT bounds, RetroPlug* plug) : IControl(bounds), _plug(plug) {
	memset(_videoScratch, 255, VIDEO_SCRATCH_SIZE);

	_settings = {
		{ "Color Correction", 2 },
		{ "High-pass Filter", 1 }
	};

	tao::json::value config;
	loadButtonConfig(config);
	_keyMap.load(config.at("gameboy"));
	_lsdjKeyMap.load(_keyMap, config.at("lsdj"));
}

void EmulatorView::OnDrop(const char* str) {
	_plug->load(EmulatorType::SameBoy, str);
}

bool EmulatorView::OnKey(const IKeyPress& key, bool down) {
	SameBoyPlugPtr plug = _plug->plug();
	if (plug) {
		if (_plug->lsdj().found && _plug->lsdj().keyboardShortcuts) {
			return _lsdjKeyMap.onKey(key, down);
		} else {
			ButtonEvent ev;
			ev.id = _keyMap.getControllerButton((VirtualKey)key.VK);
			ev.down = down;

			if (ev.id != -1) {
				_plug->setButtonState(ev);
				return true;
			}
		}
	}

	return false;
}

void EmulatorView::OnMouseDblClick(float x, float y, const IMouseMod& mod) {
	OpenLoadRomDialog();
}

void EmulatorView::OnMouseDown(float x, float y, const IMouseMod& mod) {
	if (mod.R) {
		CreateMenu(x, y);
	}
}

void EmulatorView::OnPopupMenuSelection(IPopupMenu* selectedMenu, int valIdx) {
	if (selectedMenu) {
		if (selectedMenu->GetFunction()) {
			selectedMenu->ExecFunction();
		}
	}
}

void EmulatorView::Draw(IGraphics& g) {
	SameBoyPlugPtr plugPtr = _plug->plug();
	if (plugPtr) {
		MessageBus* bus = plugPtr->messageBus();
		_lsdjKeyMap.update(bus, 33.3333333);

		size_t available = bus->video.readAvailable();
		if (available > 0) {
			// If we have multiple frames, skip to the latest
			if (available > VIDEO_FRAME_SIZE) {
				bus->video.advanceRead(available - VIDEO_FRAME_SIZE);
			}

			bus->video.read((char*)_videoScratch, VIDEO_FRAME_SIZE);

			// TODO: This is all a bit unecessary and should be handled in the SameBoy wrapper
			unsigned char* px = _videoScratch;
			for (int i = 0; i < VIDEO_WIDTH; i++) {
				for (int j = 0; j < VIDEO_HEIGHT; j++) {
					std::swap(px[0], px[2]);
					px[3] = 255;
					px += 4;
				}
			}
		}

		DrawPixelBuffer((NVGcontext*)g.GetDrawContext());
	}
}

void EmulatorView::DrawPixelBuffer(NVGcontext* vg) {
	if (_imageId == -1) {
		_imageId = nvgCreateImageRGBA(vg, VIDEO_WIDTH, VIDEO_HEIGHT, NVG_IMAGE_NEAREST, (const unsigned char*)_videoScratch);
	} else {
		nvgUpdateImage(vg, _imageId, (const unsigned char*)_videoScratch);
	}

	nvgBeginPath(vg);

	NVGpaint imgPaint = nvgImagePattern(vg, mRECT.L, mRECT.T, VIDEO_WIDTH * 2, VIDEO_HEIGHT * 2, 0, _imageId, 1.0f);
	nvgRect(vg, mRECT.L, mRECT.T, mRECT.W(), mRECT.H());
	nvgFillPaint(vg, imgPaint);
	nvgFill(vg);
}

void EmulatorView::CreateMenu(float x, float y) {
	SameBoyPlugPtr plugPtr = _plug->plug();
	if (!plugPtr) {
		return;
	}

	_menu = IPopupMenu();
	_menu.AddItem("Load ROM...", RootMenuItems::LoadRom);
	_menu.SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
		switch (indexInMenu) {
		case RootMenuItems::LoadRom: OpenLoadRomDialog(); break;
		case RootMenuItems::KeyboardMode: ToggleKeyboardMode(); break;
		}
	});

	IPopupMenu* sramMenu = new IPopupMenu();
	sramMenu->AddItem("Save", SramMenuItems::Save);
	sramMenu->AddItem("Save As...", SramMenuItems::SaveAs);
	sramMenu->AddItem("Load (and reset)...", SramMenuItems::Load);
	_menu.AddItem("SRAM", sramMenu, RootMenuItems::Sram);

	sramMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
		switch (indexInMenu) {
		case SramMenuItems::Save: _plug->saveBattery(L""); break;
		case SramMenuItems::SaveAs: OpenSaveSramDialog(); break;
		case SramMenuItems::Load: OpenLoadSramDialog(); break;
		}
	});

	IPopupMenu* settingsMenu = CreateSettingsMenu();
	/*IPopupMenu* osMenu = new IPopupMenu(0, true, { "Off", "2x", "4x" });
	osMenu->CheckItem(0, true);
	settingsMenu->AddItem("Oversampling", osMenu);
	osMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
		int amount = 1 << indexInMenu;
	});*/

	settingsMenu->AddSeparator();
	//settingsMenu->AddItem("Set as Default");
	settingsMenu->AddItem("Open Settings Folder...");
	_menu.AddItem("Settings", settingsMenu);
	settingsMenu->SetFunction([this, settingsMenu](int indexInMenu, IPopupMenu::Item* itemChosen) {
		if (indexInMenu == settingsMenu->NItems() - 1) {
			ShellExecute(NULL, NULL, getContentPath().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
	});

	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		_menu.AddSeparator();
		_menu.AddItem(plugPtr->romName().c_str(), RootMenuItems::LsdjVersion, IPopupMenu::Item::kDisabled);

		IPopupMenu* arduboyMenu = new IPopupMenu(0, true, {
			"Off",
			"MIDI Sync",
			"MIDI Sync (Arduinoboy Mode)",
			"MIDI Map",
		});

		arduboyMenu->AddSeparator();
		arduboyMenu->AddItem("Auto Play", LsdjModeMenuItems::AutoPlay, lsdj.autoPlay ? IPopupMenu::Item::kChecked : 0);

		_menu.AddItem("LSDj Sync", arduboyMenu, RootMenuItems::LsdjModes);
		_menu.AddItem("Keyboard Shortcuts", RootMenuItems::KeyboardMode, lsdj.keyboardShortcuts ? IPopupMenu::Item::kChecked : 0);

		int selectedMode = GetLsdjModeMenuItem(lsdj.syncMode);
		arduboyMenu->CheckItem(selectedMode, true);
		arduboyMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
			LsdjModeMenuItems menuItem = (LsdjModeMenuItems)indexInMenu;
			if (menuItem <= LsdjModeMenuItems::MidiMap) {
				_plug->lsdj().syncMode = GetLsdjModeFromMenu(menuItem);
			} else {
				_plug->lsdj().autoPlay = !_plug->lsdj().autoPlay;
			}
		});
	}

	GetUI()->CreatePopupMenu(*((IControl*)this), _menu, x, y);
}

IPopupMenu* EmulatorView::CreateSettingsMenu() {
	IPopupMenu* settingsMenu = new IPopupMenu();

	// TODO: These should be moved in to the SameBoy wrapper
	std::map<std::string, std::vector<std::string>> settings;
	settings["Color Correction"] = {
		"Off",
		"Correct Curves",
		"Emulate Hardware",
		"Preserve Brightness"
	};

	settings["High-pass Filter"] = {
		"Off",
		"Accurate",
		"Remove DC Offset"
	};

	for (auto& setting : settings) {
		const std::string& name = setting.first;
		IPopupMenu* settingMenu = new IPopupMenu(0, true);
		for (size_t i = 0; i < setting.second.size(); i++) {
			auto& option = setting.second[i];
			settingMenu->AddItem(option.c_str(), i);
		}

		settingMenu->CheckItem(_settings[name], true);
		settingsMenu->AddItem(name.c_str(), settingMenu);
		settingMenu->SetFunction([this, name](int indexInMenu, IPopupMenu::Item* itemChosen) {
			_settings[name] = indexInMenu;
			_plug->setSetting(name, indexInMenu);
		});
	}

	return settingsMenu;
}

void EmulatorView::ToggleKeyboardMode() {
	_plug->lsdj().keyboardShortcuts = !_plug->lsdj().keyboardShortcuts;
}

void EmulatorView::OpenLoadRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Roms", L"*.gb;*.gbc" }
	};

	std::wstring path = BasicFileOpen(types);
	if (path.size() > 0) {
		std::string p = ws2s(path);
		_plug->load(EmulatorType::SameBoy, p.c_str());
	}
}

void EmulatorView::OpenLoadSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Saves", L"*.sav" }
	};

	std::wstring path = BasicFileOpen(types);
	if (path.size() > 0) {
		_plug->loadBattery(path);
	}
}

void EmulatorView::OpenSaveSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Saves", L"*.sav" }
	};

	std::wstring path = BasicFileSave(types);
	if (path.size() > 0) {
		_plug->saveBattery(path);
	}
}
