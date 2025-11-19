#ifdef _MSC_VER
	#define __attribute__(x)
#endif

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/wasmfs.h>
// Additional includes for LSDJ enums
#include <liblsdj/liblsdj/include/lsdj/error.h>
#include <liblsdj/liblsdj/include/lsdj/channel.h>
#include <liblsdj/liblsdj/include/lsdj/instrument.h>
#include <liblsdj/liblsdj/include/lsdj/command.h>
#include <rfl/json.hpp>

#include "application/GlfwNativeWindow.h"

#include "foundation/DataBuffer.h"

#include "sameboy/Constants.h"

#include "audio/AudioBuffer.h"
#include "ui/RetroPlugView.h"
#include "core/RetroPlugProject.h"
#include "lsdj/LsdjController.h"
#include "lsdj/Ram.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"
#include "lsdj/LsdjSettings.h"
#include "lsdj/SampleUtil.h"
#include "lsdj/OffsetLookup.h"
#include "RetroPlugApplication.h"

using namespace emscripten;
using namespace rp;

RetroPlugApplication* upcastApplication(orb::app::Application& app) {
	return (RetroPlugApplication*)&app;
}

/*std::string lsdjProject_getName(rp::lsdj::Project& project) {
	return std::string(project.getName());
}*/

emscripten::val lsdjProject_getName(rp::lsdj::Project& project) {
	std::string name = std::string(project.getName());
	return emscripten::val::u8string(name.c_str());
}

// LSDJ string_view wrapper functions
emscripten::val lsdjKit_getName(rp::lsdj::Kit& kit) {
	std::string name = std::string(kit.getName());
	return emscripten::val::u8string(name.c_str());
}

emscripten::val lsdjKit_getSampleName(rp::lsdj::Kit& kit, size_t sampleIdx) {
	std::string name = std::string(kit.getSampleName(sampleIdx));
	return emscripten::val::u8string(name.c_str());
}

emscripten::val lsdjRom_getKitName(rp::lsdj::Rom& rom, size_t idx) {
	std::string name = std::string(rom.getKitName(idx));
	return emscripten::val::u8string(name.c_str());
}

emscripten::val lsdjRom_getKitSampleName(rp::lsdj::Rom& rom, size_t kitIdx, size_t sampleIdx) {
	std::string name = std::string(rom.getKitSampleName(kitIdx, sampleIdx));
	return emscripten::val::u8string(name.c_str());
}

emscripten::val lsdjRom_getFontName(rp::lsdj::Rom& rom, size_t idx) {
	std::string name = std::string(rom.getFontName(idx));
	return emscripten::val::u8string(name.c_str());
}

emscripten::val lsdjRom_getPaletteName(rp::lsdj::Rom& rom, size_t idx) {
	std::string name = std::string(rom.getPaletteName(idx));
	return emscripten::val::u8string(name.c_str());
}

// LSDJ string_view parameter wrapper functions
int32 lsdjKit_addSample(rp::lsdj::Kit& kit, const std::string& name, const orb::Uint8Buffer& data) {
	return kit.addSample(std::string_view(name), data);
}

void lsdjKit_setSampleName(rp::lsdj::Kit& kit, size_t sampleIdx, const std::string& name) {
	kit.setSampleName(sampleIdx, std::string_view(name));
}

void lsdjRom_setKitName(rp::lsdj::Rom& rom, size_t idx, const std::string& name) {
	rom.setKitName(idx, std::string_view(name));
}

void lsdjRom_setKitSampleName(rp::lsdj::Rom& rom, size_t kitIdx, size_t sampleIdx, const std::string& name) {
	rom.setKitSampleName(kitIdx, sampleIdx, std::string_view(name));
}

val AudioBuffer_getWritePointer(orb::AudioBuffer& buffer, uint32 channel) {
	return val(typed_memory_view(buffer.getSampleCount(), buffer.getWritePointer(channel)));
}

val AudioBuffer_getReadPointer(orb::AudioBuffer& buffer, uint32 channel) {
	return val(typed_memory_view(buffer.getSampleCount(), buffer.getReadPointer(channel)));
}

bool RetroPlugProject_addSystem(RetroPlugProject& project, SystemLoadComponent&& load, const SameBoyComponent& comp) {
	return project.addSystem(std::move(load), comp);
}

orb::Uint8Buffer SystemLoadEntry_data_get(SystemLoadEntry& entry) {
	return entry.data();
}

void SystemLoadEntry_data_set(SystemLoadEntry& entry, orb::Uint8Buffer value) {
	entry.data.set(value);
}

using SkipUint8Buffer = rfl::Skip<orb::Uint8Buffer>;

EMSCRIPTEN_BINDINGS(retroPlug) {
	function("convertNibblesToF32", rp::lsdj::SampleUtil::convertNibblesToF32WithRotation);
	function("convertF32ToNibbles", rp::lsdj::SampleUtil::convertF32ToNibbles);
	function("upcastApplication", &upcastApplication, return_value_policy::reference());
	function("fixRomChecksum", &GameboyUtil::fixChecksum);
	function("getRomName", +[](orb::Uint8Buffer& buffer) { return std::string(GameboyUtil::getRomName(buffer)); });
	function("getLsdjRomInfo", &rp::lsdj::OffsetLookup::getRomInfo);

	class_<semver::version>("SemverVersion")
		.constructor<>()
		.property("major", &semver::version::major)
		.property("minor", &semver::version::minor)
		.property("patch", &semver::version::patch)
		//.property("prerelease_type", &semver::version::prerelease_type)
		//.property("prerelease_number", &semver::version::prerelease_number)
	;

	class_<rp::lsdj::RomInfo>("LsdjRomInfo")
		.property("name", &rp::lsdj::RomInfo::name)
		.property("version", &rp::lsdj::RomInfo::version)
		.property("tags", &rp::lsdj::RomInfo::tags)
		.property("hash", &rp::lsdj::RomInfo::hash)
		.property("isStock", &rp::lsdj::RomInfo::isStock)
	;

	class_<MemoryAccessor>("MemoryAccessor")
		.constructor<MemoryType, orb::Uint8Buffer, size_t>()
		.function("getBuffer", select_overload<orb::Uint8Buffer&()>(&MemoryAccessor::getBuffer))
		.function("getSize", &MemoryAccessor::getSize)
		//.function("write", select_overload<size_t, const orb::Uint8Buffer&>(&MemoryAccessor::write))
	;

	value_object<LsdjServiceSettings>("NativeLsdjServiceSettings")
		.field("ramOffsets", &LsdjServiceSettings::ramOffsets)
		.field("romValid", &LsdjServiceSettings::romValid)
		.field("offsetsValid", &LsdjServiceSettings::offsetsValid)
	;

	class_<SystemLoadEntry>("NativeSystemLoadEntry")
		.constructor()
		.property("path", &SystemLoadEntry::path)
		.function("getData", &SystemLoadEntry_data_get)
		.function("setData", &SystemLoadEntry_data_set)
	;

	register_map<std::string, SystemLoadEntry>("SystemLoadEntryVector");
	register_vector<std::string>("StringVector");
	register_vector<uint32>("Uint32Vector");

	enum_<entt::entity>("Entity");

	class_<SystemLoadComponent>("NativeSystemLoadComponent")
		.constructor()
		.property("entries", &SystemLoadComponent::entries, return_value_policy::reference())
	;

	class_<RetroPlugProject>("NativeRetroPlugProject")
		.function("getProjectName", &RetroPlugProject::getProjectName)
		.function("isDirty", &RetroPlugProject::isDirty)
		.function("requiresReset", &RetroPlugProject::requiresReset)
		.function("addSystem", +[](RetroPlugProject& project, SystemLoadComponent&& config, const SameBoyComponent& component) -> uint32 {
			return (uint32)project.addSystemAsync(std::move(config), component);
		})
		.function("removeSystem", +[](RetroPlugProject& project, uint32 systemId) {
			project.removeSystem(entt::entity(systemId));
		})
		.function("resetSystem", +[](RetroPlugProject& project, uint32 systemId, bool remote) {
			return project.resetSystem(entt::entity(systemId), remote);
		})
		.function("resetSystems", +[](RetroPlugProject& project, bool remote) {
			return project.resetSystems(remote);
		})
		.function("loadConfigs", &RetroPlugProject::loadConfigs)
		.function("loadFromFile", +[](RetroPlugProject& project, const std::string& path) -> TaskId {
			return project.loadFromFileAsync(path);
		})
		.function("loadFromPaths", +[](RetroPlugProject& project, const std::vector<std::string>& paths) -> TaskId {
			PathVector fsPaths;
			for (const auto& path : paths) fsPaths.push_back(std::filesystem::path(path));
			return project.loadFromPathsAsync(fsPaths);
		})
		.function("saveToDisk", +[](RetroPlugProject& project, const std::string& path) -> TaskId {
			return project.saveToFile(path);
		})
		.function("reset", +[](RetroPlugProject& project) {
			project.reset();
		})
		.function("hasProjectPath", +[](RetroPlugProject& project) -> bool {
			return project.hasProjectPath();
		})
		.function("getProjectPath", +[](RetroPlugProject& project) -> std::string {
			return project.getProjectPath().string();
		})
		.function("getSystemMemory", +[](RetroPlugProject& project, uint32 systemId, MemoryType type, AccessType access) -> MemoryAccessor {
			return project.getSystemMemory(entt::entity(systemId), type, access);
		})
		.function("getMemoryVersion", +[](RetroPlugProject& project, uint32 systemId, MemoryType type) -> uint32 {
			return project.getMemoryVersion(entt::entity(systemId), type);
		})
		.function("subscribeToMemory", +[](RetroPlugProject& project, uint32 systemId, MemoryType type) -> void {
			project.subscribeToMemory(entt::entity(systemId), type);
		})
		.function("unsubscribeFromMemory", +[](RetroPlugProject& project, uint32 systemId, MemoryType type) -> void {
			project.unsubscribeFromMemory(entt::entity(systemId), type);
		})
		.function("getLsdjController", &RetroPlugProject::getLsdjController)
		.function("serialize", +[](RetroPlugProject& project, orb::Uint8Buffer& archive, const std::string& rootPath) {
			project.serialize(archive, rootPath);
		})
		.function("serializeJson", +[](RetroPlugProject& project, const std::string& rootPath) {
			return project.serializeJson(rootPath);
		})
		.function("deserialize", +[](RetroPlugProject& project, const orb::Uint8Buffer& archive, const std::string& rootPath) {
			return project.deserialize(archive, rootPath);
		})
		.function("deserializeJson", +[](RetroPlugProject& project, const std::string& str, const std::string& rootPath) {
			return project.deserializeJson(str, rootPath);
		})
		.function("getMountPath", +[](RetroPlugProject& project) -> std::string {
			return project.getMountPath().string();
		})
		.function("getSystemIds", &RetroPlugProject::getSystemIds)
		.property("systemCount", &RetroPlugProject::getSystemCount)
		.property("version", &RetroPlugProject::getVersion)
	;

	enum_<GameboyModel>("NativeGameboyModel")
		.value("Auto", GameboyModel::Auto)
		.value("DmgB", GameboyModel::DmgB)
		//.value("SgbNtsc", GameboyModel::SgbNtsc)
		//.value("SgbPal", GameboyModel::SgbPal)
		//.value("Sgb2", GameboyModel::Sgb2)
		.value("CgbC", GameboyModel::CgbC)
		.value("CgbE", GameboyModel::CgbE)
		.value("Agb", GameboyModel::Agb)
	;

	value_object<SameBoyComponent>("NativeSameBoyComponent")
		.field("model", &SameBoyComponent::model)
		.field("fastBoot", &SameBoyComponent::fastBoot)
	;

	class_<RetroPlugView, base<orb::View>>("RetroPlugView")
	;

	class_<RetroPlugApplication>("NativeRetroPlugApplication")
		.function("getProject", &RetroPlugApplication::getProjectPtr, allow_raw_pointers())
	;

	enum_<AccessType>("NativeAccessType")
		.value("Unknown", AccessType::Unknown)
		.value("Read", AccessType::Read)
		.value("Write", AccessType::Write)
		.value("ReadWrite", AccessType::ReadWrite)
	;

	enum_<MemoryType>("NativeMemoryType")
		.value("Ram", MemoryType::Ram)
		.value("Rom", MemoryType::Rom)
		.value("Sram", MemoryType::Sram)
		.value("Vram", MemoryType::Vram)
		.value("MAX", MemoryType::MAX)
	;

	register_map<uint32, orb::Uint8Buffer>("NativeBufferMap");
	register_vector<orb::Uint8Buffer>("NativeUint8BufferVector");

	class_<LsdjController>("NativeLsdjController")
		.function("getLsdjSav", +[](LsdjController& controller, SystemId system) -> lsdj::Sav {
			return controller.getLsdjSav((entt::entity)system);
		})
		.function("getLsdjProject", +[](LsdjController& controller, SystemId system) -> lsdj::Project {
			return controller.getLsdjProject((entt::entity)system);
		})
		.function("getNextEmptyKit", +[](LsdjController& controller, SystemId system) -> uint32 {
			return controller.getNextEmptyKit((entt::entity)system);
		})
		.function("removeKit", +[](LsdjController& controller, SystemId system, uint32 kitId) -> bool {
			return controller.removeKitComponent((entt::entity)system, kitId);
		})
		.function("updateKit", +[](LsdjController& controller, SystemId system, uint32 kitId, const std::string& data) -> bool {
			rfl::Result<LsdjKitComponent> result = rfl::json::read<LsdjKitComponent>(data);
			if (!result.has_value()) {
				spdlog::error("Failed to update kit: {}", result.error().what());
				return false;
			}

			return controller.setKitComponent((entt::entity)system, kitId, std::move(result.value()));
		})
		.function("getKitsString", +[](LsdjController& controller, SystemId system) -> std::string {
			std::vector<LsdjKitComponent> kits;
			controller.getKits((entt::entity)system, kits);
			return rfl::json::write(kits);
		})
		.function("getKitComponentString", +[](LsdjController& controller, SystemId system, uint32 kitId) -> std::string {
			const LsdjKitComponent* comp = controller.getKitComponent((entt::entity)system, kitId);
			if (comp) return rfl::json::write(*comp);


			lsdj::Rom rom = controller.getLsdjRom((entt::entity)system);
			if (rom.kitIsEmpty(kitId)) {
				return rfl::json::write(LsdjKitComponent{.id = kitId, .kit = LsdjEmptyKit{}});
			}

			lsdj::Kit kit = rom.getKit(kitId);

			try {
				return rfl::json::write(LsdjKitComponent{.id = kitId, .kit = LsdjRomKit{.name = std::string(kit.getName())}});
			} catch (const std::exception& e) {
				spdlog::error("Failed to serialize ROM kit to JSON: {}", e.what());
				return "";
			}
		})
		/*.function("setKitComponent", +[](LsdjController& controller, SystemId system, uint32 kitId, const LsdjKitComponent& component) -> bool {
			return controller.setKitComponent((entt::entity)system, kitId, component);
		})
		.function("addKitComponent", +[](LsdjController& controller, SystemId system, const LsdjKitComponent& component) -> bool {
			return controller.addKitComponent((entt::entity)system, component);
		})*/
		.function("getKitData", +[](LsdjController& controller, SystemId system, uint32 kitId) -> orb::Uint8Buffer {
			return controller.getKitData((entt::entity)system, kitId);
		})
		.function("getSynthData", +[](LsdjController& controller, SystemId system, uint32 synthId) -> orb::Uint8Buffer {
			return controller.getSynthData((entt::entity)system, synthId);
		})
		.function("setSynthData", +[](LsdjController& controller, SystemId system, uint32 synthId, const orb::Uint8Buffer& data) -> bool {
			return controller.setSynthData((entt::entity)system, synthId, data);
		})
		.function("getKitSample", +[](LsdjController& controller, SystemId system, uint32 kitId, uint32 sampleId) -> orb::Uint8Buffer {
			return controller.getKitSample((entt::entity)system, kitId, sampleId);
		})
		.function("getKitVersion", +[](LsdjController& controller, SystemId system, uint32 kitId) -> uint32 {
			return controller.getKitVersion((entt::entity)system, kitId);
		})
		.function("isLsdjLoaded", +[](LsdjController& controller, SystemId system) -> bool {
			return controller.getComponent((entt::entity)system) != nullptr;
		})
		.function("invalidateSampleCacheItem", +[](LsdjController& controller, const std::string& path) {
			controller.invalidateSampleCacheItem(path);
		})
	;

	// LSDJ enum bindings
	enum_<lsdj_error_t>("lsdj_error_t")
		.value("SUCCESS", LSDJ_SUCCESS)
		.value("READ_FAILED", LSDJ_READ_FAILED)
		.value("WRITE_FAILED", LSDJ_WRITE_FAILED)
		.value("SEEK_FAILED", LSDJ_SEEK_FAILED)
		.value("TELL_FAILED", LSDJ_TELL_FAILED)
		.value("ALLOCATION_FAILED", LSDJ_ALLOCATION_FAILED)
		.value("NO_PROJECT_AT_INDEX", LSDJ_NO_PROJECT_AT_INDEX)
		.value("DECOMPRESSION_INCORRECT_SIZE", LSDJ_DECOMPRESSION_INCORRECT_SIZE)
		.value("SRAM_INITIALIZATION_CHECK_FAILED", LSDJ_SRAM_INITIALIZATION_CHECK_FAILED)
		.value("FILE_OPEN_FAILED", LSDJ_FILE_OPEN_FAILED)
	;

	// LSDJ Rom enums
	enum_<rp::lsdj::ColorSets>("LsdjColorSets")
		.value("Normal", rp::lsdj::ColorSets::Normal)
		.value("Shaded", rp::lsdj::ColorSets::Shaded)
		.value("Alternate", rp::lsdj::ColorSets::Alternate)
		.value("Selection", rp::lsdj::ColorSets::Selection)
		.value("Scroll", rp::lsdj::ColorSets::Scroll)
	;

	// LSDJ Ram enums
	enum_<rp::lsdj::ScreenType>("LsdjScreenType")
		.value("Unknown", rp::lsdj::ScreenType::Unknown)
		.value("Song", rp::lsdj::ScreenType::Song)
		.value("Chain", rp::lsdj::ScreenType::Chain)
		.value("Phrase", rp::lsdj::ScreenType::Phrase)
		.value("Instrument", rp::lsdj::ScreenType::Instrument)
		.value("Table", rp::lsdj::ScreenType::Table)
		.value("Project", rp::lsdj::ScreenType::Project)
		.value("Wave", rp::lsdj::ScreenType::Wave)
		.value("Synth", rp::lsdj::ScreenType::Synth)
		.value("Groove", rp::lsdj::ScreenType::Groove)
	;

	enum_<lsdj_channel_t>("lsdj_channel_t")
		.value("PULSE1", LSDJ_CHANNEL_PULSE1)
		.value("PULSE2", LSDJ_CHANNEL_PULSE2)
		.value("WAVE", LSDJ_CHANNEL_WAVE)
		.value("NOISE", LSDJ_CHANNEL_NOISE)
	;

	enum_<lsdj_instrument_type_t>("lsdj_instrument_type_t")
		.value("PULSE", LSDJ_INSTRUMENT_TYPE_PULSE)
		.value("WAVE", LSDJ_INSTRUMENT_TYPE_WAVE)
		.value("KIT", LSDJ_INSTRUMENT_TYPE_KIT)
		.value("NOISE", LSDJ_INSTRUMENT_TYPE_NOISE)
	;

	enum_<lsdj_command_t>("lsdj_command_t")
		.value("NONE", LSDJ_COMMAND_NONE)
		.value("A", LSDJ_COMMAND_A)
		.value("C", LSDJ_COMMAND_C)
		.value("D", LSDJ_COMMAND_D)
		.value("E", LSDJ_COMMAND_E)
		.value("F", LSDJ_COMMAND_F)
		.value("G", LSDJ_COMMAND_G)
		.value("H", LSDJ_COMMAND_H)
		.value("K", LSDJ_COMMAND_K)
		.value("L", LSDJ_COMMAND_L)
		.value("M", LSDJ_COMMAND_M)
		.value("O", LSDJ_COMMAND_O)
		.value("P", LSDJ_COMMAND_P)
		.value("R", LSDJ_COMMAND_R)
		.value("S", LSDJ_COMMAND_S)
		.value("T", LSDJ_COMMAND_T)
		.value("V", LSDJ_COMMAND_V)
		.value("W", LSDJ_COMMAND_W)
		.value("Z", LSDJ_COMMAND_Z)
		.value("ARDUINO_BOY_N", LSDJ_COMMAND_ARDUINO_BOY_N)
		.value("ARDUINO_BOY_X", LSDJ_COMMAND_ARDUINO_BOY_X)
		.value("ARDUINO_BOY_Q", LSDJ_COMMAND_ARDUINO_BOY_Q)
		.value("ARDUINO_BOY_Y", LSDJ_COMMAND_ARDUINO_BOY_Y)
		.value("B", LSDJ_COMMAND_B)
	;

	// LSDJ Rom constants
	//constant("LSDJ_ROM_SIZE", rp::lsdj::Rom::ROM_SIZE);
	//constant("LSDJ_BANK_COUNT", rp::lsdj::Rom::BANK_COUNT);
	//constant("LSDJ_BANK_SIZE", rp::lsdj::Rom::BANK_SIZE);
	//constant("LSDJ_PALETTE_COUNT", rp::lsdj::Rom::PALETTE_COUNT);
	//constant("LSDJ_FONT_COUNT", rp::lsdj::Rom::FONT_COUNT);
	//constant("LSDJ_KIT_COUNT", rp::lsdj::Rom::KIT_COUNT);

	// Framework struct bindings
	value_object<orb::Color3>("Color3")
		.field("r", &orb::Color3::r)
		.field("g", &orb::Color3::g)
		.field("b", &orb::Color3::b)
	;

	value_object<orb::PointT<uint8>>("PointU8")
		.field("x", &orb::PointT<uint8>::x)
		.field("y", &orb::PointT<uint8>::y)
	;

	// LSDJ Rom struct bindings
	value_object<rp::lsdj::Palette::ColorSet>("LsdjPaletteColorSet")
		.field("first", &rp::lsdj::Palette::ColorSet::first)
		.field("second", &rp::lsdj::Palette::ColorSet::second)
	;

	class_<rp::lsdj::Palette>("NativeLsdjPalette")
		.function("getColor", &rp::lsdj::Palette::getColor)
	;

	class_<rp::lsdj::Font::Tile>("NativeLsdjFontTile")
	;

	class_<rp::lsdj::Font>("NativeLsdjFont")
	;

	class_<rp::lsdj::Kit>("NativeLsdjKit")
		.constructor<>()
		//.constructor<MemoryAccessor, int32>()
		.constructor<const orb::Uint8Buffer&, int32>()
		.property("index", &rp::lsdj::Kit::getIndex)
		.property("isValid", &rp::lsdj::Kit::isValid)
		.property("buffer", &rp::lsdj::Kit::getBuffer)
		.function("getName", &lsdjKit_getName)
		.function("setKitData", &rp::lsdj::Kit::setKitData)
		.function("getSampleName", &lsdjKit_getSampleName)
		.function("getSampleData", select_overload<const orb::Uint8Buffer() const>(&rp::lsdj::Kit::getSampleData))
		.function("getSampleData", select_overload<const orb::Uint8Buffer(size_t) const>(&rp::lsdj::Kit::getSampleData))
		.function("addSample", &lsdjKit_addSample)
		.function("setSampleName", &lsdjKit_setSampleName)
		.function("setSampleData", &rp::lsdj::Kit::setSampleData)
		.function("getSampleDataLength", &rp::lsdj::Kit::getSampleDataLength)
		.function("getSampleOffset", &rp::lsdj::Kit::getSampleOffset)
		.function("getSampleCount", &rp::lsdj::Kit::getSampleCount)
		.property("remainingData", &rp::lsdj::Kit::getRemainingData)
	;

	// LSDJ Ram struct bindings
	value_object<rp::lsdj::MemoryOffsets::Channel>("LsdjMemoryOffsetsChannel")
		.field("active", &rp::lsdj::MemoryOffsets::Channel::active)
		.field("songPosition", &rp::lsdj::MemoryOffsets::Channel::songPosition)
		.field("chainPosition", &rp::lsdj::MemoryOffsets::Channel::chainPosition)
		.field("phrasePosition", &rp::lsdj::MemoryOffsets::Channel::phrasePosition)
	;

	value_object<rp::lsdj::MemoryOffsets>("LsdjMemoryOffsets")
		.field("tempo", &rp::lsdj::MemoryOffsets::tempo)
		.field("cursorX", &rp::lsdj::MemoryOffsets::cursorX)
		.field("cursorY", &rp::lsdj::MemoryOffsets::cursorY)
		.field("screenX", &rp::lsdj::MemoryOffsets::screenX)
		.field("screenY", &rp::lsdj::MemoryOffsets::screenY)
	;

	// LSDJ bindings
	class_<rp::lsdj::Instrument>("NativeLsdjInstrument")
		.property("type", &rp::lsdj::Instrument::getType)
		.property("kit1", &rp::lsdj::Instrument::getKit1)
		.property("kit2", &rp::lsdj::Instrument::getKit2)
		.property("isValid", &rp::lsdj::Instrument::isValid)
		.property("index", &rp::lsdj::Instrument::getIndex)
	;

	class_<rp::lsdj::Phrase>("NativeLsdjPhrase")
		.function("getNote", &rp::lsdj::Phrase::getNote)
		.function("getInstrumentIndex", &rp::lsdj::Phrase::getInstrumentIndex)
		.function("getInstrument", &rp::lsdj::Phrase::getInstrument)
		.function("getCommand", &rp::lsdj::Phrase::getCommand)
		.function("getCommandValue", &rp::lsdj::Phrase::getCommandValue)
		.property("length", &rp::lsdj::Phrase::getLength)
		.property("index", &rp::lsdj::Phrase::getIndex)
		.property("isValid", &rp::lsdj::Phrase::isValid)
	;

	class_<rp::lsdj::Chain>("NativeLsdjChain")
		.function("getPhraseIndex", &rp::lsdj::Chain::getPhraseIndex)
		.function("getPhrase", &rp::lsdj::Chain::getPhrase)
		.function("getPhraseTransposition", &rp::lsdj::Chain::getPhraseTransposition)
		.property("phraseCount", &rp::lsdj::Chain::getPhraseCount)
		.property("index", &rp::lsdj::Chain::getIndex)
		.property("isValid", &rp::lsdj::Chain::isValid)
	;

	class_<rp::lsdj::Song>("NativeLsdjSong")
		.function("getBuffer", &rp::lsdj::Song::getBuffer)
		.function("getSynthData", &rp::lsdj::Song::getSynthData)
		.function("setSynthData", &rp::lsdj::Song::setSynthData)
		.function("getChainIndex", select_overload<uint8(lsdj_channel_t, uint8) const>(&rp::lsdj::Song::getChainIndex))
		.function("getChainIndexByChannel", select_overload<uint8(uint8, uint8) const>(&rp::lsdj::Song::getChainIndex))
		.function("getChain", &rp::lsdj::Song::getChain)
		.property("fontIndex", &rp::lsdj::Song::getFontIndex)
		.property("paletteIndex", &rp::lsdj::Song::getPaletteIndex)
		.property("chainCount", &rp::lsdj::Song::getChainCount)
		.function("isRowBookMarked", &rp::lsdj::Song::isRowBookMarked)
	;

	class_<rp::lsdj::Project>("NativeLsdjProject")
		//.constructor<>()
		.constructor<orb::Uint8Buffer&>()
		.property("version", &rp::lsdj::Project::getVersion)
		.function("getName", &lsdjProject_getName)
		.function("setName", &rp::lsdj::Project::setName)
		.function("toLsdsng", +[](rp::lsdj::Project& project) -> orb::Uint8Buffer {
			orb::Uint8Buffer buffer;
			if (!project.toLsdsng(buffer)) {
				spdlog::error("Failed to convert project to lsdsng");
				return orb::Uint8Buffer();
			}

			return buffer;
		})
		.property("song", &rp::lsdj::Project::getSong)
		.property("isValid", &rp::lsdj::Project::isValid)
		.property("index", &rp::lsdj::Project::getIndex)
	;

	class_<rp::lsdj::Sav>("NativeLsdjSav")
		.constructor<>()
		.constructor<const orb::Uint8Buffer&>()
		.function("free", &rp::lsdj::Sav::free)
		.property("isValid", &rp::lsdj::Sav::isValid)
		.function("load", select_overload<lsdj_error_t(const orb::Uint8Buffer&)>(&rp::lsdj::Sav::load))
		.function("save", select_overload<orb::Uint8Buffer()>(&rp::lsdj::Sav::save))
		.property("activeProjectCount", &rp::lsdj::Sav::getProjectCount)
		.property("totalProjectCount", &rp::lsdj::Sav::getTotalProjectCount)
		.function("getProject", &rp::lsdj::Sav::getProject)
		.property("workingProject", &rp::lsdj::Sav::getWorkingProject)
		.property("workingSong", &rp::lsdj::Sav::getWorkingSong)
		.function("setWorkingProject", &rp::lsdj::Sav::setWorkingProject)
		.function("eraseProject", &rp::lsdj::Sav::eraseProject)
		.function("findNextEmptyProject", &rp::lsdj::Sav::findNextEmptyProject)
		.function("setProject", &rp::lsdj::Sav::setProject)
	;

	class_<rp::lsdj::Rom>("NativeLsdjRom")
		.constructor<>()
		//.constructor<MemoryAccessor>()
		.constructor<const orb::Uint8Buffer&>()
		.property("isValid", &rp::lsdj::Rom::isValid)
		.function("getBankAccessor", &rp::lsdj::Rom::getBankAccessor)
		.function("getAccessor", &rp::lsdj::Rom::getAccessor, return_value_policy::reference())
		.function("updateOffsets", &rp::lsdj::Rom::updateOffsets)
		.function("kitIsEmpty", &rp::lsdj::Rom::kitIsEmpty)
		.function("getKit", &rp::lsdj::Rom::getKit)
		.function("getKitName", &lsdjRom_getKitName)
		.function("setKitName", &lsdjRom_setKitName)
		.function("getKitSampleName", &lsdjRom_getKitSampleName)
		.function("setKitSampleName", &lsdjRom_setKitSampleName)
		.function("kitSampleExists", &rp::lsdj::Rom::kitSampleExists)
		.function("getKitSampleData", &rp::lsdj::Rom::getKitSampleData)
		.function("getFontName", &lsdjRom_getFontName)
		.function("getPaletteName", &lsdjRom_getPaletteName)
		.function("getFont", select_overload<rp::lsdj::Font(size_t) const>(&rp::lsdj::Rom::getFont))
		.function("getPalette", &rp::lsdj::Rom::getPalette)
		.function("getNextEmptyKit", &rp::lsdj::Rom::getNextEmptyKit)
		.function("nextEmptyKitIdx", &rp::lsdj::Rom::nextEmptyKitIdx)
	;

	class_<rp::lsdj::Ram>("NativeLsdjRam")
		.constructor<>()
		.constructor<MemoryAccessor, const rp::lsdj::MemoryOffsets&>()
		.property("isValid", &rp::lsdj::Ram::isValid)
		.function("setData", &rp::lsdj::Ram::setData)
		.function("setOffsets", &rp::lsdj::Ram::setOffsets)
		.function("isChannelActive", &rp::lsdj::Ram::isChannelActive)
		.function("getSongPosition", &rp::lsdj::Ram::getSongPosition)
		.function("getChainPosition", &rp::lsdj::Ram::getChainPosition)
		.function("getPhrasePosition", &rp::lsdj::Ram::getPhrasePosition)
		.function("setCursorPosition", &rp::lsdj::Ram::setCursorPosition)
		.function("getCursorPosition", &rp::lsdj::Ram::getCursorPosition)
		.function("getCursorX", &rp::lsdj::Ram::getCursorX)
		.function("setCursorX", &rp::lsdj::Ram::setCursorX)
		.function("getCursorY", &rp::lsdj::Ram::getCursorY)
		.function("getScreenX", &rp::lsdj::Ram::getScreenX)
		.function("getScreenY", &rp::lsdj::Ram::getScreenY)
		.function("getScreen", &rp::lsdj::Ram::getScreen)
		.function("getTempo", &rp::lsdj::Ram::getTempo)
	;

	// Audio Buffer bindings
	class_<orb::AudioBuffer>("NativeAudioBuffer")
		.constructor<uint32, uint32, f32>()
		.function("resize", &orb::AudioBuffer::resize)
		.function("getReadPointer", &AudioBuffer_getReadPointer)
		.function("getWritePointer", &AudioBuffer_getWritePointer)
		.function("clear", &orb::AudioBuffer::clear)
		.function("clearChannel", &orb::AudioBuffer::clearChannel)
		.function("clearSamples", &orb::AudioBuffer::clearSamples)
		.function("copyFrom", select_overload<void(const orb::AudioBuffer&, uint32, uint32)>(&orb::AudioBuffer::copyFrom))
		.function("copyFromRange", select_overload<void(const orb::AudioBuffer&, uint32, uint32, uint32, uint32)>(&orb::AudioBuffer::copyFrom))
		.function("addFrom", &orb::AudioBuffer::addFrom)
		//.function("applyGain", &orb::AudioBuffer::applyGain)
		.property("channelCount", &orb::AudioBuffer::getChannelCount)
		.property("sampleCount", &orb::AudioBuffer::getSampleCount)
		.property("sizeInBytes", &orb::AudioBuffer::getSizeInBytes)
		.property("sampleRate", &orb::AudioBuffer::getSampleRate, &orb::AudioBuffer::setSampleRate)
		.function("isEmpty", &orb::AudioBuffer::isEmpty)
	;
}
