#include "EmulatorView.h"

#include "platform/FileDialog.h"
#include "platform/Path.h"
#include "util/File.h"

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

enum class GameboyModelMenuItems {
	DmgB,
	//Sgb,
	//SgbNtsc,
	//SgbPal,
	//Sgb2,
	CgbC,
	CgbE,
	Agb
};

enum class GameboyModel {
	GB_MODEL_DMG_B = 0x002,
	GB_MODEL_SGB = 0x004,
	GB_MODEL_SGB_NTSC = GB_MODEL_SGB,
	GB_MODEL_SGB_PAL = 0x1004,
	GB_MODEL_SGB2 = 0x101,
	GB_MODEL_CGB_C = 0x203,
	GB_MODEL_CGB_E = 0x205,
	GB_MODEL_AGB = 0x206,
};

IPopupMenu* createModelMenu(bool addElipses) {
	std::string elipses = addElipses ? "..." : "";

	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem(("DMG B" + elipses).c_str(), (int)GameboyModelMenuItems::DmgB);
	menu->AddItem(("CGB C" + elipses).c_str(), (int)GameboyModelMenuItems::CgbC);
	menu->AddItem(("CGB E" + elipses).c_str(), (int)GameboyModelMenuItems::CgbE);
	/*menu->AddItem(("SGB" + elipses).c_str(), (int)GameboyModelMenuItems::Sgb);
	menu->AddItem(("SGB NTSC" + elipses).c_str(), (int)GameboyModelMenuItems::SgbNtsc);
	menu->AddItem(("SGB PAL" + elipses).c_str(), (int)GameboyModelMenuItems::SgbPal);
	menu->AddItem(("SGB2" + elipses).c_str(), (int)GameboyModelMenuItems::Sgb2);*/
	menu->AddItem(("AGB" + elipses).c_str(), (int)GameboyModelMenuItems::Agb);
	return menu;
}

enum class ProjectMenuItems : int {
	Save,
	SaveAs,
	Load
};

IPopupMenu* createProjectMenu() {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Save", (int)ProjectMenuItems::Save);
	menu->AddItem("Save As...", (int)ProjectMenuItems::SaveAs);
	menu->AddItem("Load...", (int)ProjectMenuItems::Load);
	return menu;
}

enum class SramMenuItems : int {
	Save,
	SaveAs,
	Load,

	Sep1,

	Songs,
	Import
};

IPopupMenu* createSramMenu() {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Save", (int)SramMenuItems::Save);
	menu->AddItem("Save As...", (int)SramMenuItems::SaveAs);
	menu->AddItem("Load (and reset)...", (int)SramMenuItems::Load);
	return menu;
}

enum class SystemMenuItems : int {
	LoadRom,
	LoadRomAs,
	Reset,
	ResetAs
};

IPopupMenu* createSystemMenu() {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Load ROM...", (int)SystemMenuItems::LoadRom);
	menu->AddItem("Load ROM As", createModelMenu(true), (int)SystemMenuItems::LoadRomAs);
	menu->AddItem("Reset", (int)SystemMenuItems::Reset);
	menu->AddItem("Reset As", createModelMenu(false), (int)SystemMenuItems::ResetAs);
	return menu;
}

enum class RootMenuItems : int {
	RomName,

	Sep1,

	System,
	Project,
	Sram,
	Settings,
	AddInstance,

	Sep2,

	GameLink,
	SendClock = 9,

	// LSDJ Specific
	LsdjModes = 9,
	KeyboardMode
};

IPopupMenu* createInstanceMenu() {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Duplicate", (int)CreateInstanceType::Duplicate);
	menu->AddItem("Same ROM", (int)CreateInstanceType::SameRom);
	menu->AddItem("Load ROM...", (int)CreateInstanceType::LoadRom);
	return menu;
}

enum class SongMenuItems {
	Export,
	Load,
	Delete
};

IPopupMenu* createSongMenu() {
	IPopupMenu* menu = new IPopupMenu(0, true);
	menu->AddItem("Export .lsdsng...", (int)SongMenuItems::Export);
	menu->AddItem("Load (and reset)", (int)SongMenuItems::Load);
	menu->AddItem("Delete (and reset)", (int)SongMenuItems::Delete);
	return menu;
}

void EmulatorView::CreateMenu(float x, float y) {
	if (!_plug) {
		return;
	}

	_menu = IPopupMenu();

	IPopupMenu* sramMenu = createSramMenu();
	IPopupMenu* systemMenu = createSystemMenu();
	IPopupMenu* projectMenu = createProjectMenu();
	IPopupMenu* instanceMenu = createInstanceMenu();
	IPopupMenu* settingsMenu = CreateSettingsMenu();

	_menu.AddItem(_plug->romName().c_str(), (int)RootMenuItems::RomName, IPopupMenu::Item::kDisabled);
	_menu.AddSeparator((int)RootMenuItems::Sep1);

	_menu.AddItem("System", systemMenu, (int)RootMenuItems::System);
	_menu.AddItem("Project", projectMenu, (int)RootMenuItems::Project);
	_menu.AddItem("SRAM", sramMenu, (int)RootMenuItems::Sram);
	_menu.AddItem("Settings", settingsMenu, (int)RootMenuItems::Settings);
	_menu.AddItem("Add Instance", instanceMenu, (int)RootMenuItems::AddInstance);
	_menu.AddSeparator((int)RootMenuItems::Sep2);
	_menu.AddItem("Game Link", (int)RootMenuItems::GameLink, _plug->gameLink() ? IPopupMenu::Item::kChecked : 0);

	systemMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item * itemChosen) {
		switch ((SystemMenuItems)indexInMenu) {
		case SystemMenuItems::LoadRom: OpenLoadRomDialog(); break;
		case SystemMenuItems::Reset: ResetSystem(); break;
		}
	});
	
	_menu.SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
		switch ((RootMenuItems)indexInMenu) {
		case RootMenuItems::KeyboardMode: ToggleKeyboardMode(); break;
		case RootMenuItems::SendClock: _plug->setMidiSync(!_plug->midiSync()); break;
		}
	});

	sramMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
		switch ((SramMenuItems)indexInMenu) {
		case SramMenuItems::Save: _plug->saveBattery(""); break;
		case SramMenuItems::SaveAs: OpenSaveSramDialog(); break;
		case SramMenuItems::Load: OpenLoadSramDialog(); break;
		case SramMenuItems::Import: OpenLoadSongsDialog(); break;
		}
	});

	settingsMenu->SetFunction([this, settingsMenu](int indexInMenu, IPopupMenu::Item* itemChosen) {
		if (indexInMenu == settingsMenu->NItems() - 1) {
			ShellExecute(NULL, NULL, getContentPath().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
	});

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
			sramMenu->AddSeparator((int)SramMenuItems::Sep1);
			sramMenu->AddItem("Songs", (int)SramMenuItems::Songs, IPopupMenu::Item::kDisabled);
			sramMenu->AddItem("Import (and reset)...", (int)SramMenuItems::Import);

			for (size_t i = 0; i < songNames.size(); i++) {
				IPopupMenu* songMenu = createSongMenu();
				sramMenu->AddItem(songNames[i].c_str(), songMenu);

				songMenu->SetFunction([this, i](int indexInMenu, IPopupMenu::Item * itemChosen) {
					switch ((SongMenuItems)indexInMenu) {
					case SongMenuItems::Export: ExportSong(i); break;
					case SongMenuItems::Load: LoadSong(i);  break;
					case SongMenuItems::Delete: DeleteSong(i);  break;
					}
				});
			}
		}

		IPopupMenu* arduboyMenu = new IPopupMenu(0, true, {
			"Off",
			"MIDI Sync",
			"MIDI Sync (Arduinoboy Mode)",
			"MIDI Map",
		});

		arduboyMenu->AddSeparator();
		arduboyMenu->AddItem("Auto Play", LsdjModeMenuItems::AutoPlay, lsdj.autoPlay ? IPopupMenu::Item::kChecked : 0);

		_menu.AddItem("LSDj Sync", arduboyMenu, (int)RootMenuItems::LsdjModes);
		_menu.AddItem("Keyboard Shortcuts", (int)RootMenuItems::KeyboardMode, lsdj.keyboardShortcuts ? IPopupMenu::Item::kChecked : 0);

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
		_menu.AddItem("Send MIDI Clock", (int)RootMenuItems::SendClock, _plug->midiSync() ? IPopupMenu::Item::kChecked : 0);
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

	settingsMenu->AddSeparator();
	settingsMenu->AddItem("Open Settings Folder...");

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
			lsdj.exportSong(index, ws2s(path));

			/*if (songData.size() > 0) {
				std::string p = ws2s(path);
				writeFile(p, songData);
			}*/
		}
	}
}

void EmulatorView::LoadSong(int index) {
	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		lsdj.loadSong(index);
		_plug->loadBattery(lsdj.saveData, true);
	}
}

void EmulatorView::DeleteSong(int index) {
	
}

void EmulatorView::ResetSystem() {
	_plug->reset();
}

void EmulatorView::OpenLoadSongsDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"LSDj Songs", L"*.lsdsng" }
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, true);
	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		lsdj.importSongs(paths);
		_plug->loadBattery(lsdj.saveData, true);
	}
}

void EmulatorView::OpenLoadRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Roms", L"*.gb;*.gbc" }
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, false);
	if (paths.size() > 0) {
		std::string p = ws2s(paths[0]);
		HideText();
		_plug->init(p.c_str());
	}
}

void EmulatorView::OpenLoadSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Saves", L"*.sav" }
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, false);
	if (paths.size() > 0) {
		std::string p = ws2s(paths[0]);
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
