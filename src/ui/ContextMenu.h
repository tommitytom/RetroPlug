#pragma once

#include "IGraphics.h"
#include "plugs/SameBoyPlug.h"

enum class RetroPlugLayout {
	Auto,
	Row,
	Column,
	Grid
};

enum class CreateInstanceType : int {
	LoadRom,
	SameRom,
	Duplicate
};

enum class ProjectMenuItems : int {
	New,
	Load,
	Save,
	SaveAs,

	Sep1,

	SaveOptions,

	Sep2,

	AddInstance,
	RemoveInstance,
	Layout
};

enum class SaveModes {
	SaveSram,
	SaveState
};

enum class BasicMenuItems {
	LoadProject,
	LoadRom,
	LoadRomAs
};

enum class SongMenuItems {
	Export,
	Load,
	Delete
};

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

static void createBasicMenu(IPopupMenu* target, IPopupMenu* modelMenu) {
	target->AddItem("Load Project...", (int)BasicMenuItems::LoadProject);
	target->AddItem("Load ROM...", (int)BasicMenuItems::LoadRom);
	target->AddItem("Load ROM As", modelMenu, (int)BasicMenuItems::LoadRomAs);
}

static IPopupMenu* createInstanceMenu(bool loaded) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Load ROM...", (int)CreateInstanceType::LoadRom);
	menu->AddItem("Same ROM", (int)CreateInstanceType::SameRom, loaded ? 0 : IPopupMenu::Item::kDisabled);
	menu->AddItem("Duplicate", (int)CreateInstanceType::Duplicate, loaded ? 0 : IPopupMenu::Item::kDisabled);
	return menu;
}

static IPopupMenu* createLayoutMenu(RetroPlugLayout checked) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Auto", (int)RetroPlugLayout::Auto);
	menu->AddItem("Column", (int)RetroPlugLayout::Column);
	menu->AddItem("Row", (int)RetroPlugLayout::Row);
	menu->AddItem("Grid", (int)RetroPlugLayout::Grid);
	menu->CheckItemAlone((int)checked);
	return menu;
}

static IPopupMenu* createSaveOptionsMenu(SaveModes checked) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Save SRAM", (int)SaveModes::SaveSram);
	menu->AddItem("Save State", (int)SaveModes::SaveState);
	menu->CheckItemAlone((int)checked);
	return menu;
}

static IPopupMenu* createSongMenu(bool working) {
	IPopupMenu* menu = new IPopupMenu(0, true);
	menu->AddItem("Export .lsdsng...", (int)SongMenuItems::Export);
	menu->AddItem("Load (and reset)", (int)SongMenuItems::Load, working ? IPopupMenu::Item::kDisabled : 0);
	menu->AddItem("Delete (and reset)", (int)SongMenuItems::Delete, working ? IPopupMenu::Item::kDisabled : 0);
	return menu;
}

static IPopupMenu* createSyncMenu(bool disableSync, bool autoPlay) {
	int flag = disableSync ? IPopupMenu::Item::kDisabled : 0;
	IPopupMenu* syncMenu = new IPopupMenu();
	syncMenu->AddItem("Off", LsdjSyncModeMenuItems::Off, flag);
	syncMenu->AddItem("MIDI Sync", LsdjSyncModeMenuItems::MidiSync, flag);
	syncMenu->AddItem("MIDI Sync (Arduinoboy Mode)", LsdjSyncModeMenuItems::MidSyncArduinoboy, flag);
	syncMenu->AddItem("MIDI Map", LsdjSyncModeMenuItems::MidiMap, flag);
	syncMenu->AddSeparator(LsdjSyncModeMenuItems::Sep1);
	syncMenu->AddItem("Auto Play", LsdjSyncModeMenuItems::AutoPlay, autoPlay ? IPopupMenu::Item::kChecked : 0);
	return syncMenu;
}

static IPopupMenu* createModelMenu(bool addElipses) {
	std::string elipses = addElipses ? "..." : "";

	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem(("DMG B" + elipses).c_str(), (int)GameboyModel::DmgB);
	menu->AddItem(("CGB C" + elipses).c_str(), (int)GameboyModel::CgbC);
	menu->AddItem(("CGB E (default)" + elipses).c_str(), (int)GameboyModel::CgbE);
	/*menu->AddItem(("SGB NTSC" + elipses).c_str(), (int)GameboyModelMenuItems::SgbNtsc);
	menu->AddItem(("SGB PAL" + elipses).c_str(), (int)GameboyModelMenuItems::SgbPal);
	menu->AddItem(("SGB2" + elipses).c_str(), (int)GameboyModelMenuItems::Sgb2);*/
	menu->AddItem(("AGB" + elipses).c_str(), (int)GameboyModel::Agb);
	return menu;
}
