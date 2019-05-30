#pragma once

#include <string>

#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "KeyMap.h"
#include "LsdjKeyMap.h"
#include "nanovg.h"

enum LsdjModeMenuItems : int {
	Off,
	MidiSync,
	MidSyncArduinoboy,
	MidiMap,
	AutoPlay = 5
};

enum class RootMenuItems : int {
	RomName,

	Sep1,

	Project,
	System,
	Sram,
	Settings,

	Sep2,

	GameLink,

	Sep3,

	SendClock = 10,

	// LSDJ Specific
	LsdjModes = 10,
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
	LsdjModeMenuItems _lsdjMode = LsdjModeMenuItems::Off;

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

	void OpenLoadRomDialog();

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

	inline LsdjModeMenuItems GetLsdjModeMenuItem(LsdjSyncModes mode) {
		switch (mode) {
		case LsdjSyncModes::Midi: return LsdjModeMenuItems::MidiSync;
		case LsdjSyncModes::MidiArduinoboy: return LsdjModeMenuItems::MidSyncArduinoboy;
		case LsdjSyncModes::MidiMap: return LsdjModeMenuItems::MidiMap;
		default: return LsdjModeMenuItems::Off;
		}
	}

	inline LsdjSyncModes GetLsdjModeFromMenu(LsdjModeMenuItems item) {
		switch (item) {
		case LsdjModeMenuItems::MidiSync: return LsdjSyncModes::Midi;
		case LsdjModeMenuItems::MidSyncArduinoboy: return LsdjSyncModes::MidiArduinoboy;
		case LsdjModeMenuItems::MidiMap: return LsdjSyncModes::MidiMap;
		default: return LsdjSyncModes::Off;
		}
	}
};
