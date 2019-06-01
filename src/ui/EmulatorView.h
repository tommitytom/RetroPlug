#pragma once

#include <string>

#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "KeyMap.h"
#include "LsdjKeyMap.h"
#include "nanovg.h"
#include "ContextMenu.h"

const int VIDEO_WIDTH = 160;
const int VIDEO_HEIGHT = 144;
const int VIDEO_FRAME_SIZE = VIDEO_WIDTH * VIDEO_HEIGHT * 4;
const int VIDEO_SCRATCH_SIZE = VIDEO_FRAME_SIZE;

class EmulatorView {
private:
	RetroPlug* _manager = nullptr;
	SameBoyPlugPtr _plug;
	unsigned char _videoScratch[VIDEO_SCRATCH_SIZE];

	int _imageId = -1;
	NVGpaint _imgPaint;
	float _alpha = 1.0f;

	KeyMap _keyMap;
	LsdjKeyMap _lsdjKeyMap;

	IPopupMenu _menu;
	LsdjSyncModeMenuItems _lsdjMode = LsdjSyncModeMenuItems::Off;

	std::map<std::string, int> _settings;

	IRECT _area;

public:
	EmulatorView(SameBoyPlugPtr plug, RetroPlug* manager);
	~EmulatorView() {}

	void SetArea(const IRECT& area) {
		_area = area;
	}

	const IRECT& GetArea() const { return _area; }
	
	void Clear(IGraphics* graphics);

	void Setup(SameBoyPlugPtr plug, RetroPlug* manager);

	SameBoyPlugPtr Plug() { return _plug; }

	void SetAlpha(float alpha) { _alpha = alpha; }

	bool OnKey(const IKeyPress& key, bool down);

	void OnMouseDblClick(float x, float y, const IMouseMod& mod);

	void OnMouseDown(float x, float y, const IMouseMod& mod);

	void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx);

	void OnDrop(const char* str);

	void Draw(IGraphics& g);

	void CreateMenu(IPopupMenu* root, IPopupMenu* projectMenu);

	void OpenLoadRomDialog(GameboyModel model);

private:
	void DrawPixelBuffer(NVGcontext* vg);

	IPopupMenu* CreateSettingsMenu();

	IPopupMenu* CreateSystemMenu(bool loaded);

	void OpenLoadSramDialog();

	void OpenSaveSramDialog();

	void OpenLoadSongsDialog();

	void ToggleKeyboardMode();

	void ExportSong(int index);

	void LoadSong(int index);

	void DeleteSong(int index);

	void ResetSystem();

	inline LsdjSyncModeMenuItems GetLsdjModeMenuItem(LsdjSyncModes mode) {
		switch (mode) {
		case LsdjSyncModes::Midi: return LsdjSyncModeMenuItems::MidiSync;
		case LsdjSyncModes::MidiArduinoboy: return LsdjSyncModeMenuItems::MidSyncArduinoboy;
		case LsdjSyncModes::MidiMap: return LsdjSyncModeMenuItems::MidiMap;
		default: return LsdjSyncModeMenuItems::Off;
		}
	}

	inline LsdjSyncModes GetLsdjModeFromMenu(LsdjSyncModeMenuItems item) {
		switch (item) {
		case LsdjSyncModeMenuItems::MidiSync: return LsdjSyncModes::Midi;
		case LsdjSyncModeMenuItems::MidSyncArduinoboy: return LsdjSyncModes::MidiArduinoboy;
		case LsdjSyncModeMenuItems::MidiMap: return LsdjSyncModes::MidiMap;
		default: return LsdjSyncModes::Off;
		}
	}
};
