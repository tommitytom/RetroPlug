#include "EmulatorView.h"

#include "platform/FileDialog.h"
#include "platform/Path.h"
#include "platform/Shell.h"
#include "util/File.h"
#include "Buttons.h"

#include <sstream>

#include "ConfigLoader.h"
#include "rapidjson/document.h"

EmulatorView::EmulatorView(InstanceIndex idx, LuaContext* lua, RetroPlugProxy* proxy, IGraphics* graphics)
	: _index(idx), _graphics(graphics), _lua(lua), _proxy(proxy)
{
	/*_settings = {
		{ "Color Correction", 2 },
		{ "High-pass Filter", 1 }
	};*/

	for (size_t i = 0; i < 2; i++) {
		_textIds[i] = new ITextControl(IRECT(0, -100, 0, 0), "", IText(23, COLOR_WHITE));
		graphics->AttachControl(_textIds[i]);
	}
}

EmulatorView::~EmulatorView() {
	HideText();
	DeleteFrame();
}

void EmulatorView::DeleteFrame() {
	if (_imageId != -1) {
		NVGcontext* ctx = (NVGcontext*)_graphics->GetDrawContext();
		nvgDeleteImage(ctx, _imageId);
		nvgBeginPath(ctx);
		nvgRect(ctx, _area.L, _area.T, _area.W(), _area.H());
		NVGcolor black = { 0, 0, 0, 1 };
		nvgFillColor(ctx, black);
		nvgFill(ctx);

		_imageId = -1;
	}

	if (_frameBuffer) {
		delete[] _frameBuffer;
		_frameBuffer = nullptr;
		_frameBufferSize = 0;
	}
}

void EmulatorView::WriteFrame(const VideoBuffer& buffer) {
	if (buffer.data.get()) {
		if (buffer.data.count() > _frameBufferSize) {
			if (_frameBuffer) {
				delete[] _frameBuffer;
			}

			_dimensions = buffer.dimensions;

			_frameBufferSize = buffer.data.count();
			_frameBuffer = new char[_frameBufferSize];

			if (_imageId != -1) {
				NVGcontext* ctx = (NVGcontext*)_graphics->GetDrawContext();
				nvgDeleteImage(ctx, _imageId);
				_imageId = -1;
			}
		}

		memcpy(_frameBuffer, buffer.data.get(), _frameBufferSize);
		_frameDirty = true;
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

void EmulatorView::Draw(IGraphics& g, double delta) {
	NVGcontext* vg = (NVGcontext*)g.GetDrawContext();
	if (_index != NO_ACTIVE_INSTANCE) {
		DrawPixelBuffer(vg);
	}
}

void EmulatorView::DrawPixelBuffer(NVGcontext* vg) {
	if (_frameDirty && _frameBuffer) {
		if (_imageId == -1) {
			_imageId = nvgCreateImageRGBA(vg, _dimensions.w, _dimensions.h, NVG_IMAGE_NEAREST, (const unsigned char*)_frameBuffer);
		} else {
			nvgUpdateImage(vg, _imageId, (const unsigned char*)_frameBuffer);
		}

		_frameDirty = false;
	}

	if (_imageId != -1) {
		nvgBeginPath(vg);

		NVGpaint imgPaint = nvgImagePattern(vg, _area.L, _area.T, _dimensions.w * _zoom, _dimensions.h * _zoom, 0, _imageId, _alpha);
		nvgRect(vg, _area.L, _area.T, _area.W(), _area.H());
		nvgFillPaint(vg, imgPaint);
		nvgFill(vg);
	}
}

enum class SystemMenuItems : int {
	LoadRom,
	LoadRomAs,
	Reset,
	ResetAs,
	ReplaceRom,
	SaveRom,

	Sep1,

	NewSram,
	LoadSram,
	SaveSram,
	SaveSramAs,
};

void EmulatorView::CreateMenu(IPopupMenu* root, IPopupMenu* projectMenu) {
	assert(_index != NO_ACTIVE_INSTANCE);
	const EmulatorInstanceDesc* desc = _proxy->getInstance(_index);

	std::string romName = desc->romName;
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
		case SystemMenuItems::NewSram: break;// _plug->clearBattery(true); break;
		case SystemMenuItems::LoadSram: OpenLoadSramDialog(); break;
		case SystemMenuItems::SaveSram: break;// _plug->saveBattery(TSTR("")); break;
		case SystemMenuItems::SaveSramAs: OpenSaveSramDialog(); break;
		case SystemMenuItems::ReplaceRom: OpenReplaceRomDialog(); break;
		case SystemMenuItems::SaveRom: OpenSaveRomDialog(); break;
		}
	});

	if (loaded) {
		IPopupMenu* settingsMenu = CreateSettingsMenu();

		root->AddItem("Settings", settingsMenu, (int)RootMenuItems::Settings);
		root->AddSeparator((int)RootMenuItems::Sep2);
		//root->AddItem("Game Link", (int)RootMenuItems::GameLink, _plug->gameLink() ? IPopupMenu::Item::kChecked : 0);
		root->AddItem("Game Link", (int)RootMenuItems::GameLink, 0);
		root->AddSeparator((int)RootMenuItems::Sep3);

		root->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
			switch ((RootMenuItems)indexInMenu) {
			case RootMenuItems::KeyboardMode: ToggleKeyboardMode(); break;
			case RootMenuItems::GameLink: break;// _plug->setGameLink(!_plug->gameLink()); _manager->updateLinkTargets(); break;
			case RootMenuItems::SendClock: break;// _plug->setMidiSync(!_plug->midiSync()); break;
			}
		});

		settingsMenu->SetFunction([this, settingsMenu](int indexInMenu, IPopupMenu::Item * itemChosen) {
			if (indexInMenu == settingsMenu->NItems() - 1) {
				openShellFolder(getContentPath());
			}
		});

		/*Lsdj& lsdj = _plug->lsdj();
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
		}*/
	}
}

IPopupMenu* EmulatorView::CreateSettingsMenu() {
	IPopupMenu* settingsMenu = new IPopupMenu();

	// TODO: These should be moved in to the SameBoy wrapper
	/*std::map<std::string, std::vector<std::string>> settings;
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
	*/
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
	menu->AddSeparator((int)SystemMenuItems::Sep1);
	menu->AddItem("New .sav", (int)SystemMenuItems::NewSram);
	menu->AddItem("Load .sav...", (int)SystemMenuItems::LoadSram);
	menu->AddItem("Save .sav", (int)SystemMenuItems::SaveSram);
	menu->AddItem("Save .sav As...", (int)SystemMenuItems::SaveSramAs);

	resetAsModel->SetFunction([=](int idx, IPopupMenu::Item*) {
		//_plug->reset((GameboyModel)(idx + 1), true);
	});

	loadAsModel->SetFunction([=](int idx, IPopupMenu::Item*) {
		OpenLoadRomDialog((GameboyModel)(idx + 1));
	});

	return menu;
}

void EmulatorView::ToggleKeyboardMode() {
	//_plug->lsdj().keyboardShortcuts = !_plug->lsdj().keyboardShortcuts;
}

void EmulatorView::ResetSystem(bool fast) {
	//_plug->reset(_plug->model(), fast);
}

void EmulatorView::OpenReplaceRomDialog() {
	/*std::vector<FileDialogFilters> types = {
		{ TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") }
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, false);
	if (paths.size() > 0) {
		std::vector<std::byte> saveData;
		_plug->saveBattery(saveData);

		_plug->init(paths[0], _plug->model(), false);
		_plug->loadBattery(saveData, false);

		_plug->disableRendering(false);
		HideText();
	}*/
}

void EmulatorView::OpenSaveRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") }
	};

	
	tstring romName = tstr(_proxy->getInstance(_index)->romName + ".gb");
	tstring path = BasicFileSave(_graphics, types, romName);
	if (path.size() > 0) {
		/*if (!writeFile(path, _plug->romData())) {
			// Fail
		}*/
	}
}

void EmulatorView::OpenLoadRomDialog(GameboyModel model) {
	std::vector<FileDialogFilters> types = {
		{ TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") }
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, false);
	if (paths.size() > 0) {
		_lua->loadRom(_index, ws2s(paths[0]));
	}
}

void EmulatorView::DisableRendering(bool disable) {
	/*if (_plug->active()) {
		_plug->disableRendering(disable);
	}*/
}

/*void EmulatorView::LoadRom(const tstring & path) {
	_plug->init(path, GameboyModel::Auto, false);
	_plug->disableRendering(false);
	HideText();
}*/

void EmulatorView::OpenLoadSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("GameBoy Saves"), TSTR("*.sav") }
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, false);
	if (paths.size() > 0) {
		//_plug->loadBattery(paths[0], true);
	}
}

void EmulatorView::OpenSaveSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("GameBoy Saves"), TSTR("*.sav") }
	};

	tstring path = BasicFileSave(_graphics, types);
	if (path.size() > 0) {
		//_plug->saveBattery(path);
	}
}
