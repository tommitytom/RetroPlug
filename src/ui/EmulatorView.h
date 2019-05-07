#pragma once

#include <string>

#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "platform/FileDialog.h"
#include "KeyMap.h"

enum RootMenuItems : int {
	LoadRom,
	LsdjModes = 2
};

enum LsdjModeMenuItems : int {
	Off,
	Slave,
	SlaveArduinoboy,
	MidiMap
};

const int VIDEO_WIDTH = 160;
const int VIDEO_HEIGHT = 144;
const int VIDEO_FRAME_SIZE = VIDEO_WIDTH * VIDEO_HEIGHT * 4;
const int VIDEO_SCRATCH_SIZE = VIDEO_FRAME_SIZE * 4;

class EmulatorView : public IControl {
private:
	RetroPlug* _plug;
	unsigned char _videoScratch[VIDEO_SCRATCH_SIZE];

	int _imageId = -1;
	NVGpaint _imgPaint;

	KeyMap _keyMap;

	IPopupMenu _menu;
	LsdjModeMenuItems _lsdjMode = LsdjModeMenuItems::Off;

public:
	EmulatorView(IRECT bounds, RetroPlug* plug): IControl(bounds), _plug(plug) {
		memset(_videoScratch, 255, VIDEO_SCRATCH_SIZE);
		_keyMap.load();
	}

	void OnInit() override {
		auto bmp = GetUI()->LoadBitmap("IDB_PNG1");
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
		OpenFileDialog();
	}

	void OnMouseDown(float x, float y, const IMouseMod& mod) override {
		SameBoyPlugPtr plugPtr = _plug->plug();
		if (!plugPtr) {
			return;
		}

		if (mod.R) {
			_menu = IPopupMenu();
			_menu.AddItem("Load ROM...", RootMenuItems::LoadRom);

			Lsdj& lsdj = _plug->lsdj();
			if (lsdj.found) {
				IPopupMenu* arduboyMenu = new IPopupMenu(0, true, {
					"Off",
					"Slave",
					"Slave (Arduinoboy mode)",
					"MIDI Map",
				});

				int selectedMode = GetLsdjModeMenuItem(lsdj.syncMode);
				arduboyMenu->CheckItem(selectedMode, true);

				_menu.AddSeparator();
				_menu.AddItem("LSDJ Sync", arduboyMenu, RootMenuItems::LsdjModes);
			}

			GetUI()->CreatePopupMenu(_menu, x, y, this);
		}
	}

	void OnPopupMenuSelection(IPopupMenu* selectedMenu) override {
		if (selectedMenu) {
			if (selectedMenu == &_menu) {
				if (selectedMenu->GetChosenItemIdx() == RootMenuItems::LoadRom) {
					OpenFileDialog();
				}
			} else {
				_lsdjMode = (LsdjModeMenuItems)selectedMenu->GetChosenItemIdx();
				_plug->lsdj().syncMode = GetLsdjModeFromMenu(_lsdjMode);
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
	void OpenFileDialog() {
		std::wstring path = BasicFileOpen();
		if (path.size() > 0) {
			std::string p = ws2s(path);
			_plug->load(EmulatorType::SameBoy, p.c_str());
		}
	}

	LsdjModeMenuItems GetLsdjModeMenuItem(LsdjSyncModes mode) {
		switch (mode) {
		case LsdjSyncModes::Slave: return LsdjModeMenuItems::Slave;
		case LsdjSyncModes::SlaveArduinoboy: return LsdjModeMenuItems::SlaveArduinoboy;
		case LsdjSyncModes::MidiMap: return LsdjModeMenuItems::MidiMap;
		default: return LsdjModeMenuItems::Off;
		}
	}

	LsdjSyncModes GetLsdjModeFromMenu(LsdjModeMenuItems item) {
		switch (item) {
		case LsdjModeMenuItems::Slave: return LsdjSyncModes::Slave;
		case LsdjModeMenuItems::SlaveArduinoboy: return LsdjSyncModes::SlaveArduinoboy;
		case LsdjModeMenuItems::MidiMap: return LsdjSyncModes::MidiMap;
		default: return LsdjSyncModes::Off;
		}
	}
};