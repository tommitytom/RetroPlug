#pragma once

#include <string>

#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "platform/FileDialog.h"
#include "platform/Path.h"
#include "KeyMap.h"

enum RootMenuItems : int {
	LoadRom,
	Sram,
	Settings,
	LsdjVersion = 4,
	LsdjModes
};

enum LsdjModeMenuItems : int {
	Off,
	MidiSync,
	MidSyncArduinoboy,
	MidiMap
};

enum SramMenuItems : int {
	Save,
	SaveAs,
	Load
};

const int VIDEO_WIDTH = 160;
const int VIDEO_HEIGHT = 144;
const int VIDEO_FRAME_SIZE = VIDEO_WIDTH * VIDEO_HEIGHT * 4;
const int VIDEO_SCRATCH_SIZE = VIDEO_FRAME_SIZE;

class EmulatorView : public IControl {
private:
	RetroPlug* _plug;
	unsigned char _videoScratch[VIDEO_SCRATCH_SIZE];

	int _imageId = -1;
	NVGpaint _imgPaint;

	KeyMap _keyMap;

	IPopupMenu _menu;
	LsdjModeMenuItems _lsdjMode = LsdjModeMenuItems::Off;

	std::map<std::string, int> _settings;

public:
	EmulatorView(IRECT bounds, RetroPlug* plug): IControl(bounds), _plug(plug) {
		memset(_videoScratch, 255, VIDEO_SCRATCH_SIZE);
		_keyMap.load();

		_settings = {
			{ "Color Correction", 2 },
			{ "High-pass Filter", 1 }
		};
	}

	void OnInit() override {
	}

	bool IsDirty() { return true; }

	void OnDrop(const char* str) override {
		_plug->load(EmulatorType::SameBoy, str);
	}

	bool OnKeyDown(float x, float y, const IKeyPress& key) { return OnKey(key, true); }

	bool OnKeyUp(float x, float y, const IKeyPress& key) { return OnKey(key, false); }

	bool OnKey(const IKeyPress& key, bool down) {
		if (_plug->plug()) {
			ButtonEvent ev;
			ev.id = _keyMap.getControllerButton(key.VK);
			ev.down = down;

			if (ev.id != -1) {
				_plug->setButtonState(ev);
				return true;
			}
		}

		return false;
	}

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) {
		OpenLoadRomDialog();
	}

	void OnMouseDown(float x, float y, const IMouseMod& mod) override {
		if (mod.R) {
			CreateMenu(x, y);
		}
	}

	void OnPopupMenuSelection(IPopupMenu* selectedMenu) override {
		if (selectedMenu) {
			if (selectedMenu->GetFunction()) {
				selectedMenu->ExecFunction();
			}
		}
	}

	void Draw(IGraphics& g) override {
		SameBoyPlugPtr plugPtr = _plug->plug();
		if (plugPtr) {
			MessageBus* bus = plugPtr->messageBus();

			size_t available = bus->video.readAvailable();
			if (available > 0) {
				// If we have multiple frames, skip to the latest
				if (available > VIDEO_FRAME_SIZE) {
					bus->video.advanceRead(available - VIDEO_FRAME_SIZE);
				}

				bus->video.read((char*)_videoScratch, VIDEO_FRAME_SIZE);

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

	void DrawPixelBuffer(NVGcontext* vg) {
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

private:
	void CreateMenu(float x, float y) {
		SameBoyPlugPtr plugPtr = _plug->plug();
		if (!plugPtr) {
			return;
		}

		_menu = IPopupMenu();
		_menu.AddItem("Load ROM...", RootMenuItems::LoadRom);
		_menu.SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
			switch (indexInMenu) {
			case RootMenuItems::LoadRom: OpenLoadRomDialog();
			}
		});

		IPopupMenu* sramMenu = new IPopupMenu();
		sramMenu->AddItem("Save", SramMenuItems::Save);
		sramMenu->AddItem("Save As...", SramMenuItems::SaveAs);
		sramMenu->AddItem("Load (and reset)...", SramMenuItems::Load);
		_menu.AddItem("SRAM", sramMenu, RootMenuItems::Sram);

		sramMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
			switch (indexInMenu) {
			case SramMenuItems::Save: SaveSram(); break;
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

			_menu.AddItem("LSDj Sync", arduboyMenu, RootMenuItems::LsdjModes);

			int selectedMode = GetLsdjModeMenuItem(lsdj.syncMode);
			arduboyMenu->CheckItem(selectedMode, true);
			arduboyMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item* itemChosen) {
				_plug->lsdj().syncMode = GetLsdjModeFromMenu((LsdjModeMenuItems)indexInMenu);
			});
		}

		GetUI()->CreatePopupMenu(_menu, x, y, this);
	}

	IPopupMenu* CreateSettingsMenu() {
		IPopupMenu* settingsMenu = new IPopupMenu();
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

	void OpenLoadRomDialog() {
		std::vector<FileDialogFilters> types = {
			{ L"GameBoy Roms", L"*.gb;*.gbc" }
		};

		std::wstring path = BasicFileOpen(types);
		if (path.size() > 0) {
			std::string p = ws2s(path);
			_plug->load(EmulatorType::SameBoy, p.c_str());
		}
	}

	void OpenLoadSramDialog() {
		std::vector<FileDialogFilters> types = {
			{ L"GameBoy Saves", L"*.sav" }
		};

		std::wstring path = BasicFileOpen(types);
		if (path.size() > 0) {
			SameBoyPlugPtr plugPtr = _plug->plug();
			if (!plugPtr) {
				return;
			}

			std::string p = ws2s(path);

			plugPtr->lock().lock();
			plugPtr->loadBattery(p);
			plugPtr->lock().unlock();
		}
	}

	void OpenSaveSramDialog() {
		std::vector<FileDialogFilters> types = {
			{ L"GameBoy Saves", L"*.sav" }
		};

		std::wstring path = BasicFileSave(types);
		if (path.size() > 0) {
			SaveSram(path);
		}
	}

	void SaveSram(std::wstring path = L"") {
		SameBoyPlugPtr plugPtr = _plug->plug();
		if (!plugPtr) {
			return;
		}

		std::string p = ws2s(path);
		plugPtr->lock().lock();
		plugPtr->saveBattery(p);
		plugPtr->lock().unlock();
	}

	LsdjModeMenuItems GetLsdjModeMenuItem(LsdjSyncModes mode) {
		switch (mode) {
		case LsdjSyncModes::Slave: return LsdjModeMenuItems::MidiSync;
		case LsdjSyncModes::SlaveArduinoboy: return LsdjModeMenuItems::MidSyncArduinoboy;
		case LsdjSyncModes::MidiMap: return LsdjModeMenuItems::MidiMap;
		default: return LsdjModeMenuItems::Off;
		}
	}

	LsdjSyncModes GetLsdjModeFromMenu(LsdjModeMenuItems item) {
		switch (item) {
		case LsdjModeMenuItems::MidiSync: return LsdjSyncModes::Slave;
		case LsdjModeMenuItems::MidSyncArduinoboy: return LsdjSyncModes::SlaveArduinoboy;
		case LsdjModeMenuItems::MidiMap: return LsdjSyncModes::MidiMap;
		default: return LsdjSyncModes::Off;
		}
	}
};
