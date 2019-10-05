#include "EmulatorView.h"

#include "platform/FileDialog.h"
#include "platform/Path.h"
#include "platform/Shell.h"
#include "util/File.h"
#include "util/Serializer.h"
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

bool EmulatorView::OnKey(const IKeyPress& key, bool down) {
	if (_plug && _plug->active()) {
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
				if (menuItem <= LsdjSyncModeMenuItems::MidiMap) {
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
		if (lsdj.loadKit(paths[0], index, error)) {
			lsdj.patchKits(_plug->romData());
			_plug->updateRom();
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
