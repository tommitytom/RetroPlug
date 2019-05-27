#pragma once

#include <string>

#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "KeyMap.h"
#include "LsdjKeyMap.h"
#include "nanovg.h"

enum RootMenuItems : int {
	LoadRom,
	Sram,
	Settings,
	AddInstance,

	Sep1,

	SendClock = 5,

	// LSDJ Specific
	LsdjVersion = 5,
	LsdjModes,
	KeyboardMode
};

enum LsdjModeMenuItems : int {
	Off,
	MidiSync,
	MidSyncArduinoboy,
	MidiMap,
	AutoPlay = 5
};

enum SramMenuItems : int {
	Save,
	SaveAs,
	Load,

	Step1,

	Songs,
	Import
};

enum class CreateInstanceType : int {
	Duplicate,
	SameRom,
	LoadRom
};

const int VIDEO_WIDTH = 160;
const int VIDEO_HEIGHT = 144;
const int VIDEO_FRAME_SIZE = VIDEO_WIDTH * VIDEO_HEIGHT * 4;
const int VIDEO_SCRATCH_SIZE = VIDEO_FRAME_SIZE;

class EmulatorView : public IControl {
private:
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
	std::function<void(EmulatorView*, CreateInstanceType)> _duplicateCb;

	ITextControl* _textIds[2] = { nullptr, nullptr };

public:
	EmulatorView(IRECT bounds, SameBoyPlugPtr plug);

	SameBoyPlugPtr Plug() { return _plug; }

	void SetAlpha(float alpha) { _alpha = alpha; }

	void OnInit() override;

	bool IsDirty() override { return true; }

	void OnDuplicateRequest(std::function<void(EmulatorView*, CreateInstanceType)> cb) {
		_duplicateCb = cb;
	}

	bool OnKey(const IKeyPress& key, bool down);

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;

	void OnMouseDown(float x, float y, const IMouseMod& mod) override;

	void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override;

	void OnDrop(const char* str) override;

	void Draw(IGraphics& g) override;

private:
	void DrawPixelBuffer(NVGcontext* vg);

	void CreateMenu(float x, float y);

	IPopupMenu* CreateSettingsMenu();

	void OpenLoadRomDialog();

	void OpenLoadSramDialog();

	void OpenSaveSramDialog();

	void ToggleKeyboardMode();

	void HideText();

	void ExportSong(int index);

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
