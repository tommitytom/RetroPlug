#include "EmulatorView.h"

#include "platform/FileDialog.h"
#include "platform/Path.h"

#include <tao/json.hpp>

EmulatorView::EmulatorView(IRECT bounds, SameBoyPlugPtr plug) : IControl(bounds), _plug(plug) {
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

void EmulatorView::OnInit() {
	const IRECT b = GetRECT();
	float mid = b.H() / 2;
	IRECT topRow(b.L, mid - 25, b.R, mid);
	IRECT bottomRow(b.L, mid, b.R, mid + 25);

	if (!_plug || !_plug->active()) {
		_textIds[0] = new ITextControl(topRow, "Double click to", IText(23, COLOR_WHITE));
		_textIds[1] = new ITextControl(bottomRow, "load a ROM...", IText(23, COLOR_WHITE));

		GetUI()->AttachControl(_textIds[0]);
		GetUI()->AttachControl(_textIds[1]);
	}
}

void EmulatorView::OnDrop(const char* str) {
	HideText();
	_plug->init(str);
}

bool EmulatorView::OnKey(const IKeyPress& key, bool down) {
	if (_plug->active()) {
		if (_plug->lsdj().found && _plug->lsdj().keyboardShortcuts) {
			return _lsdjKeyMap.onKey(key, down);
		} else {
			ButtonEvent ev;
			ev.id = _keyMap.getControllerButton((VirtualKey)key.VK);
			ev.down = down;

			if (ev.id != ButtonTypes::MAX) {
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
	if (_plug->active()) {
		MessageBus* bus = _plug->messageBus();

		// FIXME: This constant is the delta time between frames.
		// It is set to this because on windows iPlug doesn't go higher
		// than 30fps!  Should probably add some proper time calculation here.
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

	NVGpaint imgPaint = nvgImagePattern(vg, mRECT.L, 0, VIDEO_WIDTH * 2, VIDEO_HEIGHT * 2, 0, _imageId, _alpha);
	nvgRect(vg, mRECT.L, mRECT.T, mRECT.W(), mRECT.H());
	nvgFillPaint(vg, imgPaint);
	nvgFill(vg);
}

void EmulatorView::CreateMenu(float x, float y) {
	if (!_plug) {
		return;
	}

	_menu = IPopupMenu();
	_menu.AddItem("Load ROM...", RootMenuItems::LoadRom);
	_menu.SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
		switch (indexInMenu) {
		case RootMenuItems::LoadRom: OpenLoadRomDialog(); break;
		case RootMenuItems::KeyboardMode: ToggleKeyboardMode(); break;
		case RootMenuItems::SendClock: _plug->setMidiSync(!_plug->midiSync()); break;
		}
	});

	IPopupMenu* sramMenu = new IPopupMenu();
	sramMenu->AddItem("Save", SramMenuItems::Save);
	sramMenu->AddItem("Save As...", SramMenuItems::SaveAs);
	sramMenu->AddItem("Load (and reset)...", SramMenuItems::Load);
	_menu.AddItem("SRAM", sramMenu, RootMenuItems::Sram);

	sramMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
		switch (indexInMenu) {
		case SramMenuItems::Save: _plug->saveBattery(""); break;
		case SramMenuItems::SaveAs: OpenSaveSramDialog(); break;
		case SramMenuItems::Load: OpenLoadSramDialog(); break;
		}
	});

	IPopupMenu* settingsMenu = CreateSettingsMenu();
	settingsMenu->AddSeparator();
	settingsMenu->AddItem("Open Settings Folder...");
	_menu.AddItem("Settings", settingsMenu);
	settingsMenu->SetFunction([this, settingsMenu](int indexInMenu, IPopupMenu::Item* itemChosen) {
		if (indexInMenu == settingsMenu->NItems() - 1) {
			ShellExecute(NULL, NULL, getContentPath().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
	});

	IPopupMenu* instanceMenu = new IPopupMenu();
	instanceMenu->AddItem("Duplicate", (int)CreateInstanceType::Duplicate);
	instanceMenu->AddItem("Same ROM", (int)CreateInstanceType::SameRom);
	instanceMenu->AddItem("Load ROM...", (int)CreateInstanceType::LoadRom);
	_menu.AddItem("Add Instance", instanceMenu, RootMenuItems::AddInstance);
	instanceMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item * itemChosen) {
		if (_duplicateCb) {
			_duplicateCb(this, (CreateInstanceType)indexInMenu);
		}
	});

	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		std::vector<std::string> songNames;
		lsdj.getSongNames(songNames);

		if (!songNames.empty()) {
			sramMenu->AddSeparator();
			sramMenu->AddItem("Songs", SramMenuItems::Songs, IPopupMenu::Item::kDisabled);
			sramMenu->AddItem("Import (and reset)...", SramMenuItems::Import);

			for (size_t i = 0; i < songNames.size(); i++) {
				IPopupMenu* songMenu = new IPopupMenu(0, true, {
					"Export .lsdsng...",
					"Load (and reset)",
					"Delete (and reset)"
				});

				sramMenu->AddItem(songNames[i].c_str(), songMenu);

				songMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item * itemChosen) {
					if (indexInMenu == 0) {
						// Export
						ExportSong(indexInMenu);
					} else {
						// Delete and reset
					}
				});
			}
		}

		_menu.AddSeparator();
		_menu.AddItem(_plug->romName().c_str(), RootMenuItems::LsdjVersion, IPopupMenu::Item::kDisabled);

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
	} else {
		_menu.AddSeparator();
		_menu.AddItem("Send MIDI Clock", RootMenuItems::SendClock, _plug->midiSync() ? IPopupMenu::Item::kChecked : 0);
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

void EmulatorView::HideText() {
	if (_textIds[0]) {
		_textIds[0]->SetText(IText(23, COLOR_BLACK));
		_textIds[1]->SetText(IText(23, COLOR_BLACK));
	}
}

#include "util/File.h"

void EmulatorView::ExportSong(int index) {
	std::vector<FileDialogFilters> types = {
		{ L"LSDj Songs", L"*.lsdsng" }
	};

	std::wstring path = BasicFileSave(types);
	if (path.size() == 0) {
		return;
	}

	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		lsdj.saveData.clear();
		_plug->saveBattery(lsdj.saveData);

		if (lsdj.saveData.size() > 0) {
			std::vector<char> songData;
			lsdj.exportSong(index, songData);

			if (songData.size() > 0) {
				std::string p = ws2s(path);
				writeFile(p, songData);
			}
		}
	}
}

void EmulatorView::OpenLoadRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Roms", L"*.gb;*.gbc" }
	};

	std::wstring path = BasicFileOpen(types);
	if (path.size() > 0) {
		std::string p = ws2s(path);
		HideText();
		_plug->init(p.c_str());
	}
}

void EmulatorView::OpenLoadSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Saves", L"*.sav" }
	};

	std::wstring path = BasicFileOpen(types);
	if (path.size() > 0) {
		std::string p = ws2s(path);
		_plug->loadBattery(p, true);
	}
}

void EmulatorView::OpenSaveSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Saves", L"*.sav" }
	};

	std::wstring path = BasicFileSave(types);
	if (path.size() > 0) {
		std::string p = ws2s(path);
		_plug->saveBattery(p);
	}
}
