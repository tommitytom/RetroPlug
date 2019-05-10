#pragma once

#include <string>

#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "KeyMap.h"
#include "nanovg.h"

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
	EmulatorView(IRECT bounds, RetroPlug* plug);

	void OnInit() override {}

	bool IsDirty() override { return true; }

	void OnDrop(const char* str) override;

	//bool OnKeyDown(float x, float y, const IKeyPress& key) override { return OnKey(key, true); }

	//bool OnKeyUp(float x, float y, const IKeyPress& key) override { return OnKey(key, false); }

	bool OnKey(const IKeyPress& key, bool down);

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;

	void OnMouseDown(float x, float y, const IMouseMod& mod) override;

	void OnPopupMenuSelection(IPopupMenu* selectedMenu) override;

	void Draw(IGraphics& g) override;

	void DrawPixelBuffer(NVGcontext* vg);

private:
	void CreateMenu(float x, float y);

	IPopupMenu* CreateSettingsMenu();

	void OpenLoadRomDialog();

	void OpenLoadSramDialog();

	void OpenSaveSramDialog();

	void SaveSram(std::wstring path = L"");

	inline LsdjModeMenuItems GetLsdjModeMenuItem(LsdjSyncModes mode) {
		switch (mode) {
		case LsdjSyncModes::Slave: return LsdjModeMenuItems::MidiSync;
		case LsdjSyncModes::SlaveArduinoboy: return LsdjModeMenuItems::MidSyncArduinoboy;
		case LsdjSyncModes::MidiMap: return LsdjModeMenuItems::MidiMap;
		default: return LsdjModeMenuItems::Off;
		}
	}

	inline LsdjSyncModes GetLsdjModeFromMenu(LsdjModeMenuItems item) {
		switch (item) {
		case LsdjModeMenuItems::MidiSync: return LsdjSyncModes::Slave;
		case LsdjModeMenuItems::MidSyncArduinoboy: return LsdjSyncModes::SlaveArduinoboy;
		case LsdjModeMenuItems::MidiMap: return LsdjSyncModes::MidiMap;
		default: return LsdjSyncModes::Off;
		}
	}
};
