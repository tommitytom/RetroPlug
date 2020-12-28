#include "Wrappers.h"

#include <sol/sol.hpp>
#include <iPlug2/IPlug/IPlugConstants.h>

#include "platform/Logger.h"
#include "model/Project.h"
#include "config.h"
#include "platform/Menu.h"
#include "model/ButtonStream.h"
#include "platform/FileDialog.h"

bool isNullPtr(const sol::object o) {
	switch (o.get_type()) {
		case sol::type::nil: return true;
		case sol::type::lightuserdata:
		case sol::type::userdata: {
			void* p = o.as<void*>();
			return p == nullptr;
		}
	}

	return false;
}

void luawrappers::registerCommon(sol::state& s) {
	s["isNullPtr"].set_function(isNullPtr);
	s["_RETROPLUG_VERSION"].set(PLUG_VERSION_STR);
	s["_PROJECT_VERSION"].set(PROJECT_VERSION);
	s["_consolePrint"].set_function(consoleLog);

	s.new_enum("MenuItemType",
		"None", MenuItemType::None,
		"SubMenu", MenuItemType::SubMenu,
		"Select", MenuItemType::Select,
		"MultiSelect", MenuItemType::MultiSelect,
		"Separator", MenuItemType::Separator,
		"Action", MenuItemType::Action,
		"Title", MenuItemType::Title
	);

	s.new_enum("SystemState",
		"Uninitialized", SystemState::Uninitialized,
		"Initialized", SystemState::Initialized,
		"RomMissing", SystemState::RomMissing,
		"Running", SystemState::Running
	);

	s.new_enum("SystemType",
		"Unknown", SystemType::Unknown,
		"Placeholder", SystemType::Placeholder,
		"SameBoy", SystemType::SameBoy
	);

	s.new_enum("AudioChannelRouting",
		"StereoMixDown", AudioChannelRouting::StereoMixDown,
		"TwoChannelsPerChannel", AudioChannelRouting::TwoChannelsPerChannel,
		"TwoChannelsPerInstance", AudioChannelRouting::TwoChannelsPerInstance
	);

	s.new_enum("MidiChannelRouting",
		"FourChannelsPerInstance", MidiChannelRouting::FourChannelsPerInstance,
		"OneChannelPerInstance", MidiChannelRouting::OneChannelPerInstance,
		"SendToAll", MidiChannelRouting::SendToAll
	);

	s.new_enum("SystemLayout",
		"Auto", SystemLayout::Auto,
		"Column", SystemLayout::Column,
		"Grid", SystemLayout::Grid,
		"Row", SystemLayout::Row
	);

	s.new_enum("SaveStateType",
		"Sram", SaveStateType::Sram,
		"State", SaveStateType::State
	);

	s.new_enum("GameboyModel",
		"Auto", GameboyModel::Auto,
		"Agb", GameboyModel::Agb,
		"CgbC", GameboyModel::CgbC,
		"CgbE", GameboyModel::CgbE,
		"DmgB", GameboyModel::DmgB
	);

	s.new_enum("DialogType",
		"Load", DialogType::Load,
		"Save", DialogType::Save,
		"Directory", DialogType::Directory
	);

	s.new_enum("HostType",
		"Reaper", iplug::kHostReaper,
		"ProTools", iplug::kHostProTools,
		"Cubase", iplug::kHostCubase,
		"Nuendo", iplug::kHostNuendo,
		"Sonar", iplug::kHostSonar,
		"Vegas", iplug::kHostVegas,
		"FL", iplug::kHostFL,
		"Samplitude", iplug::kHostSamplitude,
		"AbletonLive", iplug::kHostAbletonLive,
		"Tracktion", iplug::kHostTracktion,
		"NTracks", iplug::kHostNTracks,
		"MelodyneStudio", iplug::kHostMelodyneStudio,
		"VSTScanner", iplug::kHostVSTScanner,
		"AULab", iplug::kHostAULab,
		"Forte", iplug::kHostForte,
		"Chainer", iplug::kHostChainer,
		"Audition", iplug::kHostAudition,
		"Orion", iplug::kHostOrion,
		"Bias", iplug::kHostBias,
		"SAWStudio", iplug::kHostSAWStudio,
		"Logic", iplug::kHostLogic,
		"GarageBand", iplug::kHostGarageBand,
		"DigitalPerformer", iplug::kHostDigitalPerformer,
		"AudioMulch", iplug::kHostAudioMulch,
		"StudioOne", iplug::kHostStudioOne,
		"VST3TestHost", iplug::kHostVST3TestHost,
		"Ardour", iplug::kHostArdour,
		"Renoise", iplug::kHostRenoise,
		"OpenMPT", iplug::kHostOpenMPT,
		"WaveLabElements", iplug::kHostWaveLabElements,
		"WaveLab", iplug::kHostWaveLab,
		"TwistedWave", iplug::kHostTwistedWave,
		"Bitwig", iplug::kHostBitwig,
		"Reason", iplug::kHostReason,
		"GoldWave5x", iplug::kHostGoldWave5x,
		"Waveform", iplug::kHostWaveform,
		"Audacity", iplug::kHostAudacity,
		"Acoustica", iplug::kHostAcoustica,
		"PluginDoctor", iplug::kHostPluginDoctor,
		"iZotopeRX", iplug::kHostiZotopeRX,
		"SAVIHost", iplug::kHostSAVIHost,
		"BlueCat", iplug::kHostBlueCat,

		"Standalone", iplug::kHostStandalone,
		"WWW", iplug::kHostWWW,

		"Unknown", iplug::kHostUnknown
	);

	s.new_usertype<SameBoySettings>("SameBoySettings",
		"model", &SameBoySettings::model,
		"gameLink", &SameBoySettings::gameLink
	);

	s.new_usertype<Project>("NativeProject",
		"path", &Project::path,
		"systems", &Project::systems,
		"settings", &Project::settings,
		"selectedSystem", &Project::selectedSystem
	);

	s.new_usertype<Project::Settings>("ProjectSettings",
		"audioRouting", &Project::Settings::audioRouting,
		"midiRouting", &Project::Settings::midiRouting,
		"layout", &Project::Settings::layout,
		"zoom", &Project::Settings::zoom,
		"saveType", &Project::Settings::saveType,
		"packageRom", &Project::Settings::packageRom
	);

	s.new_usertype<GameboyButtonStream>("GameboyButtonStream",
		"hold", &GameboyButtonStream::hold,
		"release", &GameboyButtonStream::release,
		"releaseAll", &GameboyButtonStream::releaseAll,
		"delay", &GameboyButtonStream::delay,
		"press", &GameboyButtonStream::press,

		"holdDuration", &GameboyButtonStream::holdDuration,
		"releaseDuration", &GameboyButtonStream::releaseDuration,
		"releaseAllDuration", &GameboyButtonStream::releaseAllDuration
	);

	s.new_usertype<Select>("Select", sol::base_classes, sol::bases<MenuItemBase>());
	s.new_usertype<Action>("Action", sol::base_classes, sol::bases<MenuItemBase>());
	s.new_usertype<MultiSelect>("MultiSelect", sol::base_classes, sol::bases<MenuItemBase>());
	s.new_usertype<Title>("Title", sol::base_classes, sol::bases<MenuItemBase>());
	s.new_usertype<Separator>("Separator", sol::base_classes, sol::bases<MenuItemBase>());
	s.new_usertype<Menu>("Menu", "addItem", &Menu::addItem, sol::base_classes, sol::bases<MenuItemBase>());

	// TODO: The following should be allocated from a pool/factory
	s.create_named_table("MenuAlloc",
		"root", []() { return new Menu(); },
		"menu", [](const std::string& name, bool active, Menu* parent) { return new Menu(name, active, parent); },
		"title", [](const std::string& name) { return new Title(name); },
		"select", [](const std::string& name, bool checked, bool active, int id) { return new Select(name, checked, nullptr, active, id); },
		"action", [](const std::string& name, bool active, int id) { return new Action(name, nullptr, active, id); },
		"multiSelect", [](const sol::as_table_t<std::vector<std::string>>& items, int value, bool active, int id) { return new MultiSelect(items.value(), value, nullptr, active, id); },
		"separator", []() { return new Separator(); }
	);

	s.new_usertype<DataBuffer<char>>("DataBuffer",
		"new", sol::factories(
			[]() { return std::make_shared<DataBuffer<char>>(); },
			[](size_t size) { return std::make_shared<DataBuffer<char>>(size); }
		),
		"get", &DataBuffer<char>::get,
		"set", &DataBuffer<char>::set,
		"slice", [](DataBuffer<char>& buffer, size_t pos, size_t size) { 
			return std::make_shared<DataBuffer<char>>(buffer.slice(pos, size));
		},
		"toString", &DataBuffer<char>::toString,
		"hash", &DataBuffer<char>::hash,
		"size", &DataBuffer<char>::size,
		"clear", &DataBuffer<char>::clear,
		"resize", &DataBuffer<char>::resize,
		"reserve", &DataBuffer<char>::reserve,
		"copyTo", &DataBuffer<char>::copyTo,
		"copyFrom", &DataBuffer<char>::copyFrom,
		"clone", [](const DataBuffer<char>& buffer) {
			return std::make_shared<DataBuffer<char>>(std::move(buffer.clone()));
		},
		"readUint32", &DataBuffer<char>::readUint32,
		"readInt32", &DataBuffer<char>::readInt32
	);

	s.new_usertype<FileDialogFilters>("FileDialogFilters",
		"name", &FileDialogFilters::name,
		"extensions", &FileDialogFilters::extensions
	);

	s.new_usertype<DialogRequest>("DialogRequest",
		"new", sol::factories([]() { return std::make_shared<DialogRequest>(); }),
		"type", &DialogRequest::type,
		"filters", &DialogRequest::filters,
		"multiSelect", &DialogRequest::multiSelect,
		"fileName", &DialogRequest::fileName
	);

	s.new_usertype<MouseMod>("MouseMod",
		"left", &MouseMod::left,
		"right", &MouseMod::right
	);
}
