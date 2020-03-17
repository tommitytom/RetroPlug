#include "EmulatorView.h"

#include "platform/FileDialog.h"
#include "platform/Path.h"
#include "platform/Shell.h"
#include "util/File.h"
#include "util/Serializer.h"
#include "util/math.h"
#include "Buttons.h"

#include <sstream>

#include "ConfigLoader.h"
#include "rapidjson/document.h"

EmulatorView::EmulatorView(SameBoyPlugPtr plug, RetroPlug* manager, IGraphics* graphics)
	: _plug(plug), _manager(manager), _graphics(graphics)
{
	memset(_videoScratch, 255, VIDEO_SCRATCH_SIZE);

	_settings = {
		{ "Color Correction", 2 },
		{ "High-pass Filter", 1 }
	};

	rapidjson::Document config;
	loadButtonConfig(config);
	_keyMap.load(config["gameboy"]);
	_lsdjKeyMap.load(_keyMap, config["lsdj"]);

	for (size_t i = 0; i < 2; i++) {
		_textIds[i] = new ITextControl(IRECT(0, -100, 0, 0), "", IText(23, COLOR_WHITE));
		graphics->AttachControl(_textIds[i]);
	}
}

EmulatorView::~EmulatorView() {
	HideText();

	if (_imageId != -1) {
		NVGcontext* ctx = (NVGcontext*)_graphics->GetDrawContext();
		nvgDeleteImage(ctx, _imageId);
	}
}

void EmulatorView::ShowText(const std::string & row1, const std::string & row2) {
	_showText = true;
	_textIds[0]->SetStr(row1.c_str());
	_textIds[1]->SetStr(row2.c_str());
	UpdateTextPosition();
}

void EmulatorView::HideText() {
	_showText = false;
	UpdateTextPosition();
}

void EmulatorView::UpdateTextPosition() {
	if (_showText) {
		float mid = _area.H() / 2;
		IRECT topRow(_area.L, mid - 25, _area.R, mid);
		IRECT bottomRow(_area.L, mid, _area.R, mid + 25);
		_textIds[0]->SetTargetAndDrawRECTs(topRow);
		_textIds[1]->SetTargetAndDrawRECTs(bottomRow);
	} else {
		_textIds[0]->SetTargetAndDrawRECTs(IRECT(0, -100, 0, 0));
		_textIds[1]->SetTargetAndDrawRECTs(IRECT(0, -100, 0, 0));
	}
}

void EmulatorView::SetArea(const IRECT & area) {
	_area = area;
	UpdateTextPosition();
}

void EmulatorView::Setup(SameBoyPlugPtr plug, RetroPlug* manager) {
	_plug = plug;
	_manager = manager;
	HideText();
}

int writeExtended(VirtualKey vk, uint8_t* target) {
	switch (vk) {
	case VirtualKeys::LeftWin: 
	case VirtualKeys::RightCtrl: 
	case VirtualKeys::RightWin: 
	case VirtualKeys::Insert:
	case VirtualKeys::Home:
	case VirtualKeys::Delete:
	case VirtualKeys::End:
	case VirtualKeys::Divide:
	case VirtualKeys::LeftArrow: 
	case VirtualKeys::RightArrow: 
	case VirtualKeys::UpArrow: 
	case VirtualKeys::DownArrow: 
	case VirtualKeys::PageUp: 
	case VirtualKeys::PageDown: 
	case VirtualKeys::Sleep:
	case VirtualKeys::PrintScreen:
		target[0] = 0xE0;
		return 1;
	}

	// Unknown:
	// APPS :: make: E0, 2F ------ break: E0, F0, 2F

	// Don't have a VK:
	// R ALT :: make: E0,11 --------- break: E0,F0,11
	// KP EN :: make: E0, 5A --------- break: E0, F0, 5A

	// Print screen and pause are special cases

	return 0;
}

int getMakeCode(VirtualKey vk, uint8_t* target, bool includeExt) {
	int o = 0;
	if (includeExt) {
		o = writeExtended(vk, target);
	}

	target[o] = 0;

	switch (vk) {

	case VirtualKeys::Esc: target[o] = 0x76; break;
	case VirtualKeys::F1: target[o] = 0x05; break;
	case VirtualKeys::F2: target[o] = 0x06; break;
	case VirtualKeys::F3: target[o] = 0x04; break;
	case VirtualKeys::F4: target[o] = 0x0C; break;
	case VirtualKeys::F5: target[o] = 0x03; break;
	case VirtualKeys::F6: target[o] = 0x0B; break;
	case VirtualKeys::F7: target[o] = 0x83; break;
	case VirtualKeys::F8: target[o] = 0x0A; break;
	case VirtualKeys::F9: target[o] = 0x01; break;
	case VirtualKeys::F10: target[o] = 0x09; break;
	case VirtualKeys::F11: target[o] = 0x78; break;
	case VirtualKeys::F12: target[o] = 0x07; break;
	case VirtualKeys::Space: target[o] = 0x29; break;
	case VirtualKeys::Enter: target[o] = 0x5A; break;
	
	case VirtualKeys::Oem1: target[o] = 0x4C; break; // ;
	case VirtualKeys::Oem2: target[o] = 0x4A; break; // /
	case VirtualKeys::Oem3: target[o] = 0x0E; break; // `
	case VirtualKeys::Oem4: target[o] = 0x54; break; // [
	case VirtualKeys::Oem5: target[o] = 0x5D; break; // \ (backslash)
	case VirtualKeys::Oem6: target[o] = 0x5B; break; // ]
	case VirtualKeys::Oem7: target[o] = 0x52; break; // '

	case VirtualKeys::OemMinus: target[o] = 0x4E; break;
	case VirtualKeys::OemPlus: target[o] = 0x55; break;
	case VirtualKeys::OemPeriod: target[o] = 0x49; break;
	case VirtualKeys::OemComma: target[o] = 0x41; break;

	case VirtualKeys::Subtract: target[o] = 0x7B; break;
	case VirtualKeys::Add: target[o] = 0x79; break;
	case VirtualKeys::Divide: target[o] = 0x4A; break;
	case VirtualKeys::Multiply: target[o] = 0x7C; break;
	case VirtualKeys::Decimal: target[o] = 0x71; break;

	case VirtualKeys::NumPad0: target[o] = 0x70; break;
	case VirtualKeys::NumPad1: target[o] = 0x69; break;
	case VirtualKeys::NumPad2: target[o] = 0x72; break;
	case VirtualKeys::NumPad3: target[o] = 0x7A; break;
	case VirtualKeys::NumPad4: target[o] = 0x6B; break;
	case VirtualKeys::NumPad5: target[o] = 0x73; break;
	case VirtualKeys::NumPad6: target[o] = 0x74; break;
	case VirtualKeys::NumPad7: target[o] = 0x6C; break;
	case VirtualKeys::NumPad8: target[o] = 0x75; break;
	case VirtualKeys::NumPad9: target[o] = 0x7D; break;

	case VirtualKeys::Backspace: target[o] = 0x66; break;
	case VirtualKeys::Tab: target[o] = 0x0D; break;
	case VirtualKeys::Caps: target[o] = 0x58; break;

	case VirtualKeys::Ctrl: target[o] = 0x14; break;
	case VirtualKeys::Shift: target[o] = 0x12; break;
	case VirtualKeys::Alt: target[o] = 0x11; break;

	case VirtualKeys::LeftShift: target[o] = 0x12; break;
	case VirtualKeys::LeftCtrl: target[o] = 0x14; break;
	case VirtualKeys::LeftWin: target[o] = 0x1F; break;

	case VirtualKeys::RightShift: target[o] = 0x59; break;
	case VirtualKeys::RightCtrl: target[o] = 0x1F; break;
	case VirtualKeys::RightWin: target[o] = 0x1F; break;

	
	case VirtualKeys::Scroll: target[o] = 0x7E; break;
	case VirtualKeys::Insert: target[o] = 0x70; break;

	case VirtualKeys::Home: target[o] = 0x6C; break;
	case VirtualKeys::Delete: target[o] = 0x71; break;
	case VirtualKeys::End: target[o] = 0x69; break;

	case VirtualKeys::NumLock: target[o] = 0x77; break;

	case VirtualKeys::LeftArrow: target[o] = 0x6B; break;
	case VirtualKeys::RightArrow: target[o] = 0x74; break;
	case VirtualKeys::UpArrow: target[o] = 0x75; break;
	case VirtualKeys::DownArrow: target[o] = 0x72; break;
	case VirtualKeys::PageUp: target[o] = 0x7D; break;
	case VirtualKeys::PageDown: target[o] = 0x7A; break;

	case VirtualKeys::Num0: target[o] = 0x45; break;
	case VirtualKeys::Num1: target[o] = 0x16; break;
	case VirtualKeys::Num2: target[o] = 0x1E; break;
	case VirtualKeys::Num3: target[o] = 0x26; break;
	case VirtualKeys::Num4: target[o] = 0x25; break;
	case VirtualKeys::Num5: target[o] = 0x2E; break;
	case VirtualKeys::Num6: target[o] = 0x36; break;
	case VirtualKeys::Num7: target[o] = 0x3D; break;
	case VirtualKeys::Num8: target[o] = 0x3E; break;
	case VirtualKeys::Num9: target[o] = 0x46; break;

	case VirtualKeys::A: target[o] = 0x1C; break;
	case VirtualKeys::B: target[o] = 0x32; break;
	case VirtualKeys::C: target[o] = 0x21; break;
	case VirtualKeys::D: target[o] = 0x23; break;
	case VirtualKeys::E: target[o] = 0x24; break;
	case VirtualKeys::F: target[o] = 0x2B; break;
	case VirtualKeys::G: target[o] = 0x34; break;
	case VirtualKeys::H: target[o] = 0x33; break;
	case VirtualKeys::I: target[o] = 0x43; break;
	case VirtualKeys::J: target[o] = 0x3B; break;
	case VirtualKeys::K: target[o] = 0x42; break;
	case VirtualKeys::L: target[o] = 0x4B; break;
	case VirtualKeys::M: target[o] = 0x3A; break;
	case VirtualKeys::N: target[o] = 0x31; break;
	case VirtualKeys::O: target[o] = 0x44; break;
	case VirtualKeys::P: target[o] = 0x4D; break;
	case VirtualKeys::Q: target[o] = 0x15; break;
	case VirtualKeys::R: target[o] = 0x2D; break;
	case VirtualKeys::S: target[o] = 0x1B; break;
	case VirtualKeys::T: target[o] = 0x2C; break;
	case VirtualKeys::U: target[o] = 0x3C; break;
	case VirtualKeys::V: target[o] = 0x2A; break;
	case VirtualKeys::W: target[o] = 0x1D; break;
	case VirtualKeys::X: target[o] = 0x22; break;
	case VirtualKeys::Y: target[o] = 0x35; break;
	case VirtualKeys::Z: target[o] = 0x1A; break;

	case VirtualKeys::PrintScreen:
		target[o] = 0x12;
		target[o+1] = 0xE0;
		target[o+2] = 0x7C;
		return 3;

	case VirtualKeys::Pause:
		target[o] = 0xE1;
		target[o+1] = 0x14;
		target[o+2] = 0x77;
		target[o+3] = 0xE1;
		target[o+4] = 0xF0;
		target[o+5] = 0x14;
		target[o+6] = 0xF0;
		target[o+7] = 0x77;
		return 8;
	}

	if (target[o] != 0) {
		return o + 1;
	}

	return 0;
}

int getBreakCode(VirtualKey vk, uint8_t* target) {
	if (vk == VirtualKeys::Pause) {
		return 0;
	}

	int o = writeExtended(vk, target);
	int count = getMakeCode(vk, target + o + 1, false);
	if (count > 0) {
		target[o] = 0xF0;
		return o + count + 1;
	}

	return 0;
}

bool EmulatorView::OnKey(const IKeyPress& key, bool down) {
	if (_plug && _plug->active()) {
		Lsdj& lsdj = _plug->lsdj();
		if (lsdj.found) {
			if (lsdj.syncMode == LsdjSyncModes::Keyboard) {
				uint8_t scancodes[8];
				int count = 0;
				if (down == true) {
					count = getMakeCode((VirtualKey)key.VK, scancodes, true);
				} else {
					count = getBreakCode((VirtualKey)key.VK, scancodes);
				}

				const double LSDJ_PS2_BYTE_DELAY = 5.0;

				if (count) {
					int accum = (int)((_plug->sampleRate() / 1000.0) * LSDJ_PS2_BYTE_DELAY);
					
					const std::string* vkname = VirtualKeys::toString((VirtualKey)key.VK);
					std::string vkn = (vkname ? *vkname : "Unknown");

					std::cout << (down == true ? "KEY DOWN |" : "KEY UP   |");
					std::cout << std::hex << " Char: " << key.utf8[0] << " | VK: " << vkn << " | PS/2: [ ";

					std::stringstream lsdjCodes;
					lsdjCodes << std::hex;

					int offset = 0;
					for (int i = 0; i < count; ++i) {
						std::cout << "0x" << (int)(uint8_t)scancodes[i] << (i < count - 1 ? ", " : " ");
						lsdjCodes << "0x" << (int)(uint8_t)(reverse(scancodes[i]) >> 1) << (i < count - 1 ? ", " : " ");
						_plug->sendSerialByte(offset, reverse(scancodes[i]) >> 1);
						offset += accum;
					}

					std::cout << "] | LSDj: [ " << lsdjCodes.str() << "]" << std::endl;
				}

				return true;
			} else if (lsdj.keyboardShortcuts) {
				return _lsdjKeyMap.onKey(key, down);
			}
		}

		ButtonEvent ev;
		ev.id = _keyMap.getControllerButton((VirtualKey)key.VK);
		ev.down = down;

		if (ev.id != ButtonTypes::MAX) {
			_plug->setButtonState(ev);
			return true;
		}
	}

	return false;
}

void EmulatorView::Draw(IGraphics& g) {
	if (_plug && _plug->active()) {
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

	_fileWatcher.update();
}

void EmulatorView::DrawPixelBuffer(NVGcontext* vg) {
	if (_imageId == -1) {
		_imageId = nvgCreateImageRGBA(vg, VIDEO_WIDTH, VIDEO_HEIGHT, NVG_IMAGE_NEAREST, (const unsigned char*)_videoScratch);
	} else {
		nvgUpdateImage(vg, _imageId, (const unsigned char*)_videoScratch);
	}

	nvgBeginPath(vg);

	NVGpaint imgPaint = nvgImagePattern(vg, _area.L, _area.T, VIDEO_WIDTH * 2, VIDEO_HEIGHT * 2, 0, _imageId, _alpha);
	nvgRect(vg, _area.L, _area.T, _area.W(), _area.H());
	nvgFillPaint(vg, imgPaint);
	nvgFill(vg);
}

enum class SystemMenuItems : int {
	LoadRom,
	LoadRomAs,
	Reset,
	ResetAs,
	ReplaceRom,
	SaveRom,
	WatchRom,

	Sep1,

	NewSram,
	LoadSram,
	SaveSram,
	SaveSramAs,
};

void EmulatorView::CreateMenu(IPopupMenu* root, IPopupMenu* projectMenu) {
	if (!_plug) {
		return;
	}

	std::string romName = _plug->romName();
	bool loaded = !romName.empty();
	if (!loaded) {
		romName = "No ROM Loaded";
	}

	IPopupMenu* systemMenu = CreateSystemMenu();
	
	root->AddItem(romName.c_str(), (int)RootMenuItems::RomName, IPopupMenu::Item::kTitle);
	root->AddSeparator((int)RootMenuItems::Sep1);

	root->AddItem("Project", projectMenu, (int)RootMenuItems::Project);
	root->AddItem("System", systemMenu, (int)RootMenuItems::System);

	systemMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item * itemChosen) {
		switch ((SystemMenuItems)indexInMenu) {
		case SystemMenuItems::LoadRom: OpenLoadRomDialog(GameboyModel::Auto); break;
		case SystemMenuItems::Reset: ResetSystem(true); break;
		case SystemMenuItems::NewSram: _plug->clearBattery(true); break;
		case SystemMenuItems::LoadSram: OpenLoadSramDialog(); break;
		case SystemMenuItems::SaveSram: _plug->saveBattery(T("")); break;
		case SystemMenuItems::SaveSramAs: OpenSaveSramDialog(); break;
		case SystemMenuItems::ReplaceRom: OpenReplaceRomDialog(); break;
		case SystemMenuItems::SaveRom: OpenSaveRomDialog(); break;
		case SystemMenuItems::WatchRom: ToggleWatchRom(); break;
		}
	});

	if (loaded) {
		IPopupMenu* settingsMenu = CreateSettingsMenu();

		root->AddItem("Settings", settingsMenu, (int)RootMenuItems::Settings);
		root->AddSeparator((int)RootMenuItems::Sep2);
		root->AddItem("Game Link", (int)RootMenuItems::GameLink, _plug->gameLink() ? IPopupMenu::Item::kChecked : 0);
		root->AddSeparator((int)RootMenuItems::Sep3);

		root->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
			switch ((RootMenuItems)indexInMenu) {
			case RootMenuItems::KeyboardMode: ToggleKeyboardMode(); break;
			case RootMenuItems::GameLink: _plug->setGameLink(!_plug->gameLink()); _manager->updateLinkTargets(); break;
			case RootMenuItems::SendClock: _plug->setMidiSync(!_plug->midiSync()); break;
			}
		});

		settingsMenu->SetFunction([this, settingsMenu](int indexInMenu, IPopupMenu::Item * itemChosen) {
			if (indexInMenu == settingsMenu->NItems() - 1) {
				openShellFolder(getContentPath());
			}
		});

		Lsdj& lsdj = _plug->lsdj();
		if (lsdj.found) {
			IPopupMenu* syncMenu = createSyncMenu(_plug->gameLink(), lsdj.autoPlay);
			root->AddItem("LSDj Sync", syncMenu, (int)RootMenuItems::LsdjModes);

			std::vector<LsdjSongName> songNames;
			_plug->saveBattery(lsdj.saveData);
			
			lsdj.getSongNames(songNames);

			if (!songNames.empty()) {
				IPopupMenu* songMenu = new IPopupMenu();
				songMenu->AddItem("Import (and reset)...");
				songMenu->AddItem("Export All...");
				songMenu->AddSeparator();

				root->AddItem("LSDj Songs", songMenu, (int)RootMenuItems::LsdjSongs);

				for (size_t i = 0; i < songNames.size(); i++) {
					IPopupMenu* songItemMenu = createSongMenu(songNames[i].projectId == -1);
					songMenu->AddItem(songNames[i].name.c_str(), songItemMenu);

					songItemMenu->SetFunction([=](int indexInMenu, IPopupMenu::Item* itemChosen) {
						int id = songNames[i].projectId;
						switch ((SongMenuItems)indexInMenu) {
						case SongMenuItems::Export: ExportSong(songNames[i]); break;
						case SongMenuItems::Load: LoadSong(id); break;
						case SongMenuItems::Delete: DeleteSong(id); break;
						}
					});
				}

				songMenu->SetFunction([=](int idx, IPopupMenu::Item*) {
					switch (idx) {
					case 0: OpenLoadSongsDialog(); break;
					case 1: ExportSongs(songNames); break;
					}
				});
			}

			std::vector<std::string> kitNames;
			lsdj.getKitNames(kitNames);

			IPopupMenu* kitMenu = new IPopupMenu();
			kitMenu->AddItem("Import...");
			kitMenu->AddItem("Export All...");
			kitMenu->AddSeparator();

			for (size_t i = 0; i < kitNames.size(); i++) {
				IPopupMenu* kitItemMenu = createKitMenu(kitNames[i] == "Empty");

				std::stringstream ss;
				ss << std::hex << i + 1 << ": " << kitNames[i];
				kitMenu->AddItem(ss.str().c_str(), kitItemMenu);

				kitItemMenu->SetFunction([=](int indexInMenu, IPopupMenu::Item* itemChosen) {
					switch ((KitMenuItems)indexInMenu) {
					case KitMenuItems::Load: LoadKit(i); break;
					case KitMenuItems::Export: ExportKit(i); break;
					case KitMenuItems::Delete: DeleteKit(i); break;
					}
				});
			}

			root->AddItem("LSDj Kits", kitMenu, (int)RootMenuItems::LsdjKits);

			kitMenu->SetFunction([=](int idx, IPopupMenu::Item*) {
				switch (idx) {
				case 0: OpenLoadKitsDialog(); break;
				case 1: ExportKits(); break;
				}
			});
			
			root->AddItem("Keyboard Shortcuts", (int)RootMenuItems::KeyboardMode, lsdj.keyboardShortcuts ? IPopupMenu::Item::kChecked : 0);

			int selectedMode = GetLsdjModeMenuItem(lsdj.syncMode);
			syncMenu->CheckItem(selectedMode, true);
			syncMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
				LsdjSyncModeMenuItems menuItem = (LsdjSyncModeMenuItems)indexInMenu;
				if (menuItem < LsdjSyncModeMenuItems::Sep1) {
					_plug->lsdj().syncMode = GetLsdjModeFromMenu(menuItem);
				} else {
					_plug->lsdj().autoPlay = !_plug->lsdj().autoPlay;
				}
			});
		} else {
			root->AddItem("Send MIDI Clock", (int)RootMenuItems::SendClock, _plug->midiSync() ? IPopupMenu::Item::kChecked : 0);
		}
	}
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
		IPopupMenu* settingMenu = new IPopupMenu();
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

IPopupMenu* EmulatorView::CreateSystemMenu() {
	IPopupMenu* loadAsModel = createModelMenu(true);
	IPopupMenu* resetAsModel = createModelMenu(false);

	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Load ROM...", (int)SystemMenuItems::LoadRom);
	menu->AddItem("Load ROM As", loadAsModel, (int)SystemMenuItems::LoadRomAs);
	menu->AddItem("Reset", (int)SystemMenuItems::Reset);
	menu->AddItem("Reset As", resetAsModel, (int)SystemMenuItems::ResetAs);
	menu->AddItem("Replace ROM...", (int)SystemMenuItems::ReplaceRom);
	menu->AddItem("Save ROM...", (int)SystemMenuItems::SaveRom);
	menu->AddItem("Reset on ROM changes", (int)SystemMenuItems::WatchRom, _watchId != 0 ? IPopupMenu::Item::kChecked : 0);
	menu->AddSeparator((int)SystemMenuItems::Sep1);
	menu->AddItem("New .sav", (int)SystemMenuItems::NewSram);
	menu->AddItem("Load .sav...", (int)SystemMenuItems::LoadSram);
	menu->AddItem("Save .sav", (int)SystemMenuItems::SaveSram);
	menu->AddItem("Save .sav As...", (int)SystemMenuItems::SaveSramAs);

	resetAsModel->SetFunction([=](int idx, IPopupMenu::Item*) {
		_plug->reset((GameboyModel)(idx + 1), true);
	});

	loadAsModel->SetFunction([=](int idx, IPopupMenu::Item*) {
		OpenLoadRomDialog((GameboyModel)(idx + 1));
	});

	return menu;
}

void EmulatorView::ToggleKeyboardMode() {
	_plug->lsdj().keyboardShortcuts = !_plug->lsdj().keyboardShortcuts;
}

void EmulatorView::ExportSong(const LsdjSongName& songName) {
	std::vector<FileDialogFilters> types = {
		{ T("LSDj Songs"), T("*.lsdsng") }
	};

	tstring path = BasicFileSave(_graphics, types, tstr(songName.name + "." + std::to_string(songName.version)));
	if (path.size() == 0) {
		return;
	}

	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		lsdj.saveData.clear();
		_plug->saveBattery(lsdj.saveData);

		if (lsdj.saveData.size() > 0) {
			std::vector<std::byte> songData;
			lsdj.exportSong(songName.projectId, songData);

			if (songData.size() > 0) {
				writeFile(path, songData);
			}
		}
	}
}

void EmulatorView::ExportSongs(const std::vector<LsdjSongName>& songNames) {
	std::vector<tstring> paths = BasicFileOpen(_graphics, {}, false, true);
	if (paths.size() > 0) {
		Lsdj& lsdj = _plug->lsdj();
		if (lsdj.found) {
			lsdj.saveData.clear();
			_plug->saveBattery(lsdj.saveData);
			
			if (lsdj.saveData.size() > 0) {
				std::vector<NamedData> songData;
				lsdj.exportSongs(songData);

				for (const auto& song : songData) {
					fs::path p(paths[0]);
					p /= song.name + ".lsdsng";
					writeFile(tstr(p.wstring()), song.data);
				}
			}
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
	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		lsdj.deleteSong(index);
		_plug->loadBattery(lsdj.saveData, false);
	}
}

void EmulatorView::LoadKit(int index) {
	std::vector<FileDialogFilters> types = {
		{ T("LSDj Kits"), T("*.kit") }
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, false);
	if (paths.size() > 0) {
		std::string error;
		Lsdj& lsdj = _plug->lsdj();
		bool exists = lsdj.kitData[index] != nullptr;
		if (lsdj.loadKit(paths[0], index, error)) {
			lsdj.patchKits(_plug->romData());
			_plug->updateRom();
			
			if (!exists) {
				_plug->reset(_plug->model(), true);
			}
		} else {
			_graphics->ShowMessageBox(error.c_str(), "Import Failed", kMB_OK);
		}
	}
}

void EmulatorView::DeleteKit(int index) {
	Lsdj& lsdj = _plug->lsdj();
	lsdj.deleteKit(_plug->romData(), index);
	_plug->updateRom();
}

void EmulatorView::ExportKit(int index) {
	std::vector<FileDialogFilters> types = {
		{ T("LSDj Kit"), T("*.kit") }
	};

	std::vector<std::string> kitNames;
	_plug->lsdj().getKitNames(kitNames);

	tstring path = BasicFileSave(_graphics, types, tstr(kitNames[index]));
	if (path.size() == 0) {
		return;
	}

	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		std::vector<std::byte> kitData;
		lsdj.exportKit(_plug->romData(), index, kitData);

		if (kitData.size() > 0) {
			writeFile(path, kitData);
		}
	}
}

void EmulatorView::ExportKits() {
	std::vector<tstring> paths = BasicFileOpen(_graphics, {}, false, true);
	if (paths.size() > 0) {
		Lsdj& lsdj = _plug->lsdj();
		if (lsdj.found) {
			for (auto kit : lsdj.kitData) {
				if (kit) {
					fs::path p(paths[0]);
					p /= kit->name + ".kit";
					writeFile(tstr(p.wstring()), kit->data);
				}
			}
		}
	}
}

void EmulatorView::ResetSystem(bool fast) {
	_plug->reset(_plug->model(), fast);
}

void EmulatorView::ToggleWatchRom() {
	if (_romListener == nullptr) {
		_romListener = std::make_unique<RomUpdateListener>(_plug);
		std::string path = fs::path(_plug->romPath()).parent_path().string();
		_watchId = _fileWatcher.addWatch(path.c_str(), _romListener.get());
		_plug->setWatchRom(true);
	} else {
		_fileWatcher.removeWatch(_watchId);
		_watchId = 0;
		_romListener = nullptr;
		_plug->setWatchRom(false);
	}	
}

void EmulatorView::OpenLoadSongsDialog() {
	std::vector<FileDialogFilters> types = {
		{ T("All Supported Types"), T("*.lsdsng;*.sav") },
        { T("LSDj Songs"), T("*.lsdsng") },
		{ T("LSDj .sav"), T("*.sav") }
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, true);
	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		std::string error;
		std::vector<int> ids = lsdj.importSongs(paths, error);
		if (ids.size() > 0) {
			_plug->loadBattery(lsdj.saveData, false);
		}

		if (error.size() > 0) {
			_graphics->ShowMessageBox(error.c_str(), "Import Failed", kMB_OK);
		}
	}
}

void EmulatorView::OpenLoadKitsDialog() {
	std::vector<FileDialogFilters> types = {
		{ T("All Supported Types"), T("*.gb;*.gbc;*.kit") },
		{ T("LSDj Kits"), T("*.kit") },
		{ T("GameBoy Roms"), T("*.gb;*.gbc") },
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, true);
	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		std::string error;

		for (auto& path : paths) {
			fs::path p = path;
			if (p.extension() == ".kit") {
				if (lsdj.loadKit(path, -1, error)) {
					continue;
				}
			} else {
				std::vector<std::byte> f;
				if (readFile(path, f)) {
					if (lsdj.loadRomKits(f, false, error)) {
						continue;
					}
				}
			}

			_graphics->ShowMessageBox(error.c_str(), "Import Failed", kMB_OK);
		}

		lsdj.patchKits(_plug->romData());
		_plug->updateRom();
	}
}

void EmulatorView::OpenReplaceRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ T("GameBoy Roms"), T("*.gb;*.gbc") }
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, false);
	if (paths.size() > 0) {
		std::vector<NamedHashedDataPtr> kits;

		if (_plug->lsdj().found) {
			kits = _plug->lsdj().kitData;
		}

		std::vector<std::byte> saveData;
		_plug->saveBattery(saveData);

		_plug->init(paths[0], _plug->model(), false);
		_plug->loadBattery(saveData, false);
		
		if (_plug->lsdj().found) {
			_plug->lsdj().kitData = kits;
			_plug->lsdj().patchKits(_plug->romData());
			_plug->updateRom();
		}

		_plug->disableRendering(false);
		HideText();
	}
}

void EmulatorView::OpenSaveRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ T("GameBoy Roms"), T("*.gb;*.gbc") }
	};

	tstring romName = tstr(_plug->romName() + ".gb");
	tstring path = BasicFileSave(_graphics, types, romName);
	if (path.size() > 0) {
		if (!writeFile(path, _plug->romData())) {
			// Fail
		}
	}
}

void EmulatorView::OpenLoadRomDialog(GameboyModel model) {
	std::vector<FileDialogFilters> types = {
		{ T("GameBoy Roms"), T("*.gb;*.gbc") }
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, false);
	if (paths.size() > 0) {
		_plug->init(paths[0], model, false);
		_plug->disableRendering(false);
		HideText();
	}
}

void EmulatorView::DisableRendering(bool disable) {
	if (_plug->active()) {
		_plug->disableRendering(disable);
	}
}

void EmulatorView::LoadRom(const tstring & path) {
	_plug->init(path, GameboyModel::Auto, false);
	_plug->disableRendering(false);
	HideText();
}

void EmulatorView::OpenLoadSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ T("GameBoy Saves"), T("*.sav") }
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, false);
	if (paths.size() > 0) {
		_plug->loadBattery(paths[0], true);
	}
}

void EmulatorView::OpenSaveSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ T("GameBoy Saves"), T("*.sav") }
	};

	tstring path = BasicFileSave(_graphics, types);
	if (path.size() > 0) {
		_plug->saveBattery(path);
	}
}
