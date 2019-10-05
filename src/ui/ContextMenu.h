#pragma once

#include "IGraphics.h"
#include "plugs/RetroPlug.h"

using namespace iplug;
using namespace igraphics;

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
	Layout,

	Sep3,

	AudioRouting,
	MidiRouting
};

enum class BasicMenuItems {
	LoadProject,
	LoadRom,
	LoadRomAs
};

enum class SongMenuItems {
	Load,
	Export,
	Delete
};

enum class KitMenuItems {
	Load,
	Export,
	Delete
};

enum LsdjSyncModeMenuItems : int {
	Off,
	MidiSync,
	MidSyncArduinoboy,
	MidiMap,
	//KeyboardModeArduinoboy,

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

	SendClock = 8,

	// LSDJ Specific
	LsdjModes = 8,
	LsdjKits,
	LsdjSongs,
	KeyboardMode
};

static void createBasicMenu(IPopupMenu* target, IPopupMenu* modelMenu) {
	target->AddItem("Load Project...", (int)BasicMenuItems::LoadProject);
	target->AddItem("Load ROM...", (int)BasicMenuItems::LoadRom);
	target->AddItem("Load ROM As", modelMenu, (int)BasicMenuItems::LoadRomAs);
}

static IPopupMenu* createInstanceMenu(bool loaded, bool enabled) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Load ROM...", (int)CreateInstanceType::LoadRom, enabled ? 0 : IPopupMenu::Item::kDisabled);
	menu->AddItem("Same ROM", (int)CreateInstanceType::SameRom, enabled && loaded ? 0 : IPopupMenu::Item::kDisabled);
	menu->AddItem("Duplicate", (int)CreateInstanceType::Duplicate, enabled && loaded ? 0 : IPopupMenu::Item::kDisabled);
	return menu;
}

static IPopupMenu* createLayoutMenu(InstanceLayout checked) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Auto", (int)InstanceLayout::Auto);
	menu->AddItem("Column", (int)InstanceLayout::Column);
	menu->AddItem("Row", (int)InstanceLayout::Row);
	menu->AddItem("Grid", (int)InstanceLayout::Grid);
	menu->CheckItemAlone((int)checked);
	return menu;
}

static IPopupMenu* createSaveOptionsMenu(SaveStateType checked) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Save State", (int)SaveStateType::State);
	menu->AddItem("Save SRAM", (int)SaveStateType::Sram);
	menu->CheckItemAlone((int)checked);
	return menu;
}

static IPopupMenu* createAudioRoutingMenu(AudioChannelRouting mode) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Stereo Mixdown", (int)AudioChannelRouting::StereoMixDown);
	menu->AddItem("Two Channels Per Instance", (int)AudioChannelRouting::TwoChannelsPerInstance);
	menu->CheckItemAlone((int)mode);
	return menu;
}

static IPopupMenu* createMidiRoutingMenu(MidiChannelRouting mode) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("All Channels to All Instances", (int)MidiChannelRouting::SendToAll);
	menu->AddItem("Four Channels Per Instance", (int)MidiChannelRouting::FourChannelsPerInstance);
	menu->AddItem("One Channel Per Instance", (int)MidiChannelRouting::OneChannelPerInstance);
	menu->CheckItemAlone((int)mode);
	return menu;
}

static IPopupMenu* createSongMenu(bool working) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Load (and reset)", (int)SongMenuItems::Load, working ? IPopupMenu::Item::kDisabled : 0);
	menu->AddItem("Export .lsdsng...", (int)SongMenuItems::Export);
	menu->AddItem("Delete", (int)SongMenuItems::Delete, working ? IPopupMenu::Item::kDisabled : 0);
	return menu;
}

static IPopupMenu* createKitMenu(bool empty) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem(!empty ? "Replace..." : "Load (and reset)...", (int)KitMenuItems::Load);
	menu->AddItem("Export .kit...", (int)KitMenuItems::Export, empty ? IPopupMenu::Item::kDisabled : 0);
	menu->AddItem("Delete", (int)KitMenuItems::Delete, empty ? IPopupMenu::Item::kDisabled : 0);
	return menu;
}

static IPopupMenu* createSyncMenu(bool disableSync, bool autoPlay) {
	int flag = disableSync ? IPopupMenu::Item::kDisabled : 0;
	IPopupMenu* syncMenu = new IPopupMenu();
	syncMenu->AddItem("Off", LsdjSyncModeMenuItems::Off, flag);
	syncMenu->AddItem("MIDI Sync", LsdjSyncModeMenuItems::MidiSync, flag);
	syncMenu->AddItem("MIDI Sync (Arduinoboy Variation)", LsdjSyncModeMenuItems::MidSyncArduinoboy, flag);
	syncMenu->AddItem("MIDI Map", LsdjSyncModeMenuItems::MidiMap, flag);
	//syncMenu->AddItem("Keyboard Mode (Arduinoboy Variation)", LsdjSyncModeMenuItems::KeyboardModeArduinoboy, flag);
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
