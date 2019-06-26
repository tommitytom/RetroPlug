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
	IGraphics* _graphics;

	bool _showText = false;
	ITextControl* _textIds[2] = { nullptr };

public:
	EmulatorView(SameBoyPlugPtr plug, RetroPlug* manager, IGraphics* graphics);
	~EmulatorView();

	void ShowText(const std::string& row1, const std::string& row2);

	void HideText();

	void UpdateTextPosition();

	void SetArea(const IRECT& area);

	const IRECT& GetArea() const { return _area; }

	void Setup(SameBoyPlugPtr plug, RetroPlug* manager);

	SameBoyPlugPtr Plug() { return _plug; }

	void SetAlpha(float alpha) { _alpha = alpha; }

	bool OnKey(const IKeyPress& key, bool down);

	void Draw(IGraphics& g);

	void CreateMenu(IPopupMenu* root, IPopupMenu* projectMenu);

	void OpenLoadRomDialog(GameboyModel model);

	void DisableRendering(bool disable);

	void LoadRom(const std::wstring& path);

private:
	void DrawPixelBuffer(NVGcontext* vg);

	IPopupMenu* CreateSettingsMenu();

	IPopupMenu* CreateSystemMenu();

	void OpenLoadSramDialog();

	void OpenSaveSramDialog();

	void OpenLoadSongsDialog();

	void OpenLoadDialog();

	void ToggleKeyboardMode();

	void ExportSong(const LsdjSongName& songName);

	void ExportSongs(const std::vector<LsdjSongName>& songNames);

	void LoadSong(int index);

	void DeleteSong(int index);

	void ResetSystem(bool fast);

	inline LsdjSyncModeMenuItems GetLsdjModeMenuItem(LsdjSyncModes mode) {
		switch (mode) {
		case LsdjSyncModes::Midi: return LsdjSyncModeMenuItems::MidiSync;
		case LsdjSyncModes::MidiArduinoboy: return LsdjSyncModeMenuItems::MidSyncArduinoboy;
		case LsdjSyncModes::MidiMap: return LsdjSyncModeMenuItems::MidiMap;
		case LsdjSyncModes::KeyboardArduinoboy: return LsdjSyncModeMenuItems::KeyboardModeArduinoboy;
		default: return LsdjSyncModeMenuItems::Off;
		}
	}

	inline LsdjSyncModes GetLsdjModeFromMenu(LsdjSyncModeMenuItems item) {
		switch (item) {
		case LsdjSyncModeMenuItems::MidiSync: return LsdjSyncModes::Midi;
		case LsdjSyncModeMenuItems::MidSyncArduinoboy: return LsdjSyncModes::MidiArduinoboy;
		case LsdjSyncModeMenuItems::MidiMap: return LsdjSyncModes::MidiMap;
		case LsdjSyncModeMenuItems::KeyboardModeArduinoboy: return LsdjSyncModes::KeyboardArduinoboy;
		default: return LsdjSyncModes::Off;
		}
	}
};
