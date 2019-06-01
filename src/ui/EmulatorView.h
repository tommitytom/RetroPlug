#pragma once

#include <string>

#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "KeyMap.h"
#include "LsdjKeyMap.h"
#include "nanovg.h"

enum LsdjSyncModeMenuItems : int {
	Off,
	MidiSync,
	MidSyncArduinoboy,
	MidiMap,

	Sep1, 

	AutoPlay
};

enum class RootMenuItems : int {
	RomName,

	Sep1,

	Project,
	System,
	Settings,

	Sep2,

	GameLink,

	Sep3,

	SendClock = 10,

	// LSDJ Specific
	LsdjModes = 10,
	LsdjSongs,
	KeyboardMode
};

const int VIDEO_WIDTH = 160;
const int VIDEO_HEIGHT = 144;
const int VIDEO_FRAME_SIZE = VIDEO_WIDTH * VIDEO_HEIGHT * 4;
const int VIDEO_SCRATCH_SIZE = VIDEO_FRAME_SIZE;

class EmulatorView : public IControl {
public:
	std::function<void(IPopupMenu*, bool)> OnProjectMenuRequest;

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

	bool _showText = false;
	ITextControl* _textIds[2] = { nullptr, nullptr };

public:
	EmulatorView(IRECT bounds, SameBoyPlugPtr plug, RetroPlug* manager);
	~EmulatorView();

	void Setup(SameBoyPlugPtr plug, RetroPlug* manager);

	SameBoyPlugPtr Plug() { return _plug; }

	void SetAlpha(float alpha) { _alpha = alpha; }

	void OnInit() override;

	bool IsDirty() override { return true; }

	bool OnKey(const IKeyPress& key, bool down);

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;

	void OnMouseDown(float x, float y, const IMouseMod& mod) override;

	void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override;

	void OnDrop(const char* str) override;

	void Draw(IGraphics& g) override;

	void ShowText(bool show);

	void UpdateTextPosition();

private:
	void DrawPixelBuffer(NVGcontext* vg);

	void CreateMenu(float x, float y);

	IPopupMenu* CreateSettingsMenu();

	IPopupMenu* CreateSystemMenu(bool loaded);

	void OpenLoadRomDialog(GameboyModel model);

	void OpenLoadSramDialog();

	void OpenSaveSramDialog();

	void OpenLoadSongsDialog();

	void ToggleKeyboardMode();

	void ExportSong(int index);

	void LoadSong(int index);

	void DeleteSong(int index);

	void ResetSystem();

	void SaveProject();
	void SaveProjectAs();
	void LoadProject();

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
