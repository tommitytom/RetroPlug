#include "application/GlfwNativeWindow.h"

#ifdef _MSC_VER
	#define __attribute__(x)
#endif

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include "core/Project.h"
#include "core/ProxySystem.h"
#include "core/ProxySystemService.h"
#include "foundation/DataBuffer.h"
#include "ui/RetroPlugView.h"
#include "lsdj/Ram.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"
#include "lsdj/LsdjSettings.h"
#include "sameboy/Constants.h"
#include "core/audio/Effect.h"
#include "core/audio/BiquadEffect.h"
#include "core/audio/DitherEffect.h"
#include "core/audio/EffectChain.h"

// Additional includes for LSDJ enums
#include <liblsdj/liblsdj/include/lsdj/error.h>
#include <liblsdj/liblsdj/include/lsdj/channel.h>
#include <liblsdj/liblsdj/include/lsdj/instrument.h>
#include <liblsdj/liblsdj/include/lsdj/command.h>

using namespace emscripten;
using namespace rp;


#include <emscripten/wasmfs.h>

std::shared_ptr<RetroPlugView> upcastView(const fw::ViewPtr& view) {
	return std::static_pointer_cast<RetroPlugView>(view);
}

/*std::string lsdjProject_getName(rp::lsdj::Project& project) {
	return std::string(project.getName());
}*/

emscripten::val lsdjProject_getName(rp::lsdj::Project& project) {
	std::string name = std::string(project.getName());
	return emscripten::val::u8string(name.c_str());
}

emscripten::val system_getRomName(rp::System& system) {
	std::string name = std::string(system.getRomName());
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
int32 lsdjKit_addSample(rp::lsdj::Kit& kit, const std::string& name, const fw::Uint8Buffer& data) {
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

val AudioBuffer_getWritePointer(fw::AudioBuffer& buffer, uint32 channel) {
	return val(typed_memory_view(buffer.getSampleCount(), buffer.getWritePointer(channel)));
}

val AudioBuffer_getReadPointer(fw::AudioBuffer& buffer, uint32 channel) {
	return val(typed_memory_view(buffer.getSampleCount(), buffer.getReadPointer(channel)));
}

ProxySystemServicePtr ProxySystem_findService(ProxySystem& system, SystemServiceType type) {
	for (const auto& service : system.getServices()) {
		if (service->getType() == type) {
			return std::static_pointer_cast<ProxySystemService>(service);
		}
	}

	return nullptr;
}

#include "lsdj/SampleUtil.h"

void setupWasmFs() {
	//backend_t opfs = wasmfs_create_opfs_backend();
	//spdlog::info("Created OPFS backend");
	//int err = wasmfs_create_directory("/opfs", 0777, opfs);
	//spdlog::info("Created OPFS directory: {}", err == 0 ? "success" : "failed");
}

EMSCRIPTEN_BINDINGS(retroPlug) {
	function("setupWasmFs", &setupWasmFs);

	function("convertNibblesToF32", rp::lsdj::SampleUtil::convertNibblesToF32);
	function("convertF32ToNibbles", rp::lsdj::SampleUtil::convertF32ToNibbles);

	constant("SAMEBOY_GUID", SAMEBOY_GUID);
	constant("INVALID_SYSTEM_ID", INVALID_SYSTEM_ID);

	// Bindings for SystemPaths
	value_object<SystemPaths>("SystemPaths")
		.field("romPath", &SystemPaths::romPath)
		.field("sramPath", &SystemPaths::sramPath)
		.field("statePath", &SystemPaths::statePath)
	;

	value_object<SystemSettings>("SystemSettings")
		.field("includeRom", &SystemSettings::includeRom)
		.field("gameLink", &SystemSettings::gameLink)
		.field("reloadRomOnChange", &SystemSettings::reloadRomOnChange)
	;

	value_object<SystemDesc>("SystemDesc")
		.field("paths", &SystemDesc::paths)
		.field("settings", &SystemDesc::settings)
		//.field("services", &SystemDesc::services)
	;

	enum_<SaveStateType>("SaveStateType")
        .value("None", SaveStateType::None)
        .value("Sram", SaveStateType::Sram)
        .value("State", SaveStateType::State)
    ;

	value_object<LoadConfig>("LoadConfig")
		.field("desc", &LoadConfig::desc)
		.field("romBuffer", &LoadConfig::romBuffer)
		.field("sramBuffer", &LoadConfig::sramBuffer)
		.field("stateBuffer", &LoadConfig::stateBuffer)
		.field("stateType", &LoadConfig::stateType)
		.field("reset", &LoadConfig::reset)
	;

	class_<MemoryAccessor>("MemoryAccessor")
		.constructor<MemoryType, fw::Uint8Buffer, size_t>()
		.function("getBuffer", &MemoryAccessor::getBuffer)
		//.function("write", select_overload<size_t, const fw::Uint8Buffer&>(&MemoryAccessor::write))
	;

	class_<System>("NativeSystem")
		.smart_ptr<std::shared_ptr<System>>("NativeSystemPtr")
		.function("reset", &System::reset)
		.function("getMemory", &System::getMemory)
		.function("getRomName", &System::getRomName)
		.property("desc", &System::getDesc)
		.property("version", &System::getVersion)
		.property("id", &System::getId)
		.function("incrementVersion", &System::incrementVersion)
	;

	value_array<SystemStateHashes>("NativeSystemStateHashes")
		.element(emscripten::index<0>())
		.element(emscripten::index<1>())
		.element(emscripten::index<2>())
		.element(emscripten::index<3>())
		.element(emscripten::index<4>())
	;

	class_<ProxySystem, base<System>>("NativeProxySystem")
		.smart_ptr<std::shared_ptr<ProxySystem>>("NativeProxySystemPtr")
		.function("getStateHashes", &ProxySystem::getStateHashes)
		.function("findService", &ProxySystem_findService)
	;

	value_object<LsdjServiceSettings>("NativeLsdjServiceSettings")
		.field("ramOffsets", &LsdjServiceSettings::ramOffsets)
		.field("romValid", &LsdjServiceSettings::romValid)
		.field("offsetsValid", &LsdjServiceSettings::offsetsValid)
	;

	class_<ProxySystemService>("NativeProxySystemService")
		.smart_ptr<std::shared_ptr<ProxySystemService>>("NativeProxySystemServicePtr")
	;

	class_<Project>("NativeProject")
		.function("addSystem", select_overload<ProxySystemPtr(SystemType, const SystemDesc&, SystemId)>(&Project::addSystem))
		.function("loadSystem", select_overload<ProxySystemPtr(SystemType, LoadConfig&&, SystemId)>(&Project::addSystem))
		.function("getSystem", &Project::getSystem)
		.function("getSystemByIndex", &Project::getSystemByIndex)
		.property("systemCount", &Project::getSystemCount)
		.function("duplicateSystem", &Project::duplicateSystem)
		.function("removeSystem", &Project::removeSystem)
		.property("version", &Project::getVersion)
		.property("scale", &Project::getScale)
		.function("clear", &Project::clear)
		.property("isDirty", &Project::isDirty, &Project::setDirty)
	;

	class_<RetroPlugView, base<fw::View>>("RetroPlugView")
		.smart_ptr<std::shared_ptr<RetroPlugView>>("RetroPlugViewPtr")
		.function("getProject", &RetroPlugView::getProject, return_value_policy::reference())
		.function("getLsdjState", &RetroPlugView::getLsdjState)
	;

	enum_<AccessType>("NativeAccessType")
		.value("Unknown", AccessType::Unknown)
		.value("Read", AccessType::Read)
		.value("Write", AccessType::Write)
		.value("ReadWrite", AccessType::ReadWrite)
	;

	enum_<MemoryType>("NativeMemoryType")
		.value("Unknown", MemoryType::Unknown)
		.value("Ram", MemoryType::Ram)
		.value("Rom", MemoryType::Rom)
		.value("Sram", MemoryType::Sram)
		.value("Vram", MemoryType::Vram)
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
	value_object<fw::Color3>("Color3")
		.field("r", &fw::Color3::r)
		.field("g", &fw::Color3::g)
		.field("b", &fw::Color3::b)
	;

	value_object<fw::PointT<uint8>>("PointU8")
		.field("x", &fw::PointT<uint8>::x)
		.field("y", &fw::PointT<uint8>::y)
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
		.constructor<MemoryAccessor, int32>()
		.property("index", &rp::lsdj::Kit::getIndex)
		.property("isValid", &rp::lsdj::Kit::isValid)
		.property("buffer", &rp::lsdj::Kit::getBuffer)
		.function("getName", &lsdjKit_getName)
		.function("setKitData", &rp::lsdj::Kit::setKitData)
		.function("getSampleName", &lsdjKit_getSampleName)
		.function("getSampleData", &rp::lsdj::Kit::getSampleData)
		.function("addSample", &lsdjKit_addSample)
		.function("setSampleName", &lsdjKit_setSampleName)
		.function("setSampleData", &rp::lsdj::Kit::setSampleData)
		.function("getSampleDataLength", &rp::lsdj::Kit::getSampleDataLength)
		.function("getSampleOffset", &rp::lsdj::Kit::getSampleOffset)
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
		.property("index", &rp::lsdj::Phrase::getIndex)
		.property("isValid", &rp::lsdj::Phrase::isValid)
	;

	class_<rp::lsdj::Chain>("NativeLsdjChain")
		.function("getPhraseIndex", &rp::lsdj::Chain::getPhraseIndex)
		.function("getPhrase", &rp::lsdj::Chain::getPhrase)
		.function("getPhraseTransposition", &rp::lsdj::Chain::getPhraseTransposition)
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
		.function("isRowBookMarked", &rp::lsdj::Song::isRowBookMarked)
	;

	class_<rp::lsdj::Project>("NativeLsdjProject")
		.property("version", &rp::lsdj::Project::getVersion)
		.function("getName", &lsdjProject_getName)
		.property("song", &rp::lsdj::Project::getSong)
		.property("isValid", &rp::lsdj::Project::isValid)
	;

	class_<rp::lsdj::Sav>("NativeLsdjSav")
		.constructor<>()
		.constructor<const fw::Uint8Buffer&>()
		.function("free", &rp::lsdj::Sav::free)
		.property("isValid", &rp::lsdj::Sav::isValid)
		.function("load", select_overload<lsdj_error_t(const fw::Uint8Buffer&)>(&rp::lsdj::Sav::load))
		.function("save", select_overload<fw::Uint8Buffer()>(&rp::lsdj::Sav::save))
		.property("projectCount", &rp::lsdj::Sav::getProjectCount)
		.function("getProject", &rp::lsdj::Sav::getProject)
		.property("workingProject", &rp::lsdj::Sav::getWorkingProject)
		.property("workingSong", &rp::lsdj::Sav::getWorkingSong)
		.function("setWorkingProject", &rp::lsdj::Sav::setWorkingProject)
	;

	class_<rp::lsdj::Rom>("NativeLsdjRom")
		.constructor<>()
		.constructor<MemoryAccessor>()
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
	class_<fw::AudioBuffer>("AudioBuffer")
		.constructor<uint32, uint32, f32>()
		.function("resize", &fw::AudioBuffer::resize)
		.function("getReadPointer", &AudioBuffer_getReadPointer)
		.function("getWritePointer", &AudioBuffer_getWritePointer)
		.function("clear", &fw::AudioBuffer::clear)
		.function("clearChannel", &fw::AudioBuffer::clearChannel)
		.function("clearSamples", &fw::AudioBuffer::clearSamples)
		.function("copyFrom", select_overload<void(const fw::AudioBuffer&, uint32, uint32)>(&fw::AudioBuffer::copyFrom))
		.function("copyFromRange", select_overload<void(const fw::AudioBuffer&, uint32, uint32, uint32, uint32)>(&fw::AudioBuffer::copyFrom))
		.function("addFrom", &fw::AudioBuffer::addFrom)
		.function("applyGain", &fw::AudioBuffer::applyGain)
		.property("channelCount", &fw::AudioBuffer::getChannelCount)
		.property("sampleCount", &fw::AudioBuffer::getSampleCount)
		.property("sizeInBytes", &fw::AudioBuffer::getSizeInBytes)
		.property("sampleRate", &fw::AudioBuffer::getSampleRate, &fw::AudioBuffer::setSampleRate)
		.function("isEmpty", &fw::AudioBuffer::isEmpty)
	;

	// Effect base class
	class_<Effect>("Effect")
		.smart_ptr<std::shared_ptr<Effect>>("EffectPtr")
		.function("process", &Effect::process, pure_virtual())
	;

	// FilterType enum
	enum_<FilterType>("FilterType")
		.value("LowPass", FilterType::LowPass)
		.value("HighPass", FilterType::HighPass)
		.value("BandPass", FilterType::BandPass)
		.value("BandStop", FilterType::BandStop)
		.value("Peak", FilterType::Peak)
		.value("LowShelf", FilterType::LowShelf)
		.value("HighShelf", FilterType::HighShelf)
		.value("AllPass", FilterType::AllPass)
	;

	// BiquadEffect class
	class_<BiquadEffect, base<Effect>>("BiquadEffect")
		.constructor<>()
		.function("process", &BiquadEffect::process)
		.function("setFilterType", &BiquadEffect::setFilterType)
		.function("setFrequency", &BiquadEffect::setFrequency)
		.function("setQ", &BiquadEffect::setQ)
		.function("setGain", &BiquadEffect::setGain)
		.function("setSampleRate", &BiquadEffect::setSampleRate)
		.function("getFilterType", &BiquadEffect::getFilterType)
		.function("getFrequency", &BiquadEffect::getFrequency)
		.function("getQ", &BiquadEffect::getQ)
		.function("getGain", &BiquadEffect::getGain)
		.function("getSampleRate", &BiquadEffect::getSampleRate)
		.function("reset", &BiquadEffect::reset)
		.function("configureLowPass", &BiquadEffect::configureLowPass)
		.function("configureHighPass", &BiquadEffect::configureHighPass)
		.function("configureBandPass", &BiquadEffect::configureBandPass)
		.function("configureBandStop", &BiquadEffect::configureBandStop)
		.function("configurePeaking", &BiquadEffect::configurePeaking)
		.function("configureLowShelf", &BiquadEffect::configureLowShelf)
		.function("configureHighShelf", &BiquadEffect::configureHighShelf)
		.function("isStable", &BiquadEffect::isStable)
		.function("getMagnitudeResponse", &BiquadEffect::getMagnitudeResponse)
	;

	// DitherMode enum
	enum_<DitherMode>("DitherMode")
		.value("ErrorDiffusion", DitherMode::ErrorDiffusion)
		.value("SierraLite", DitherMode::SierraLite)
		.value("HighPassTPDF", DitherMode::HighPassTPDF)
		.value("ShapedTPDF2ndOrder", DitherMode::ShapedTPDF2ndOrder)
		.value("JJNErrorDiffusion", DitherMode::JJNErrorDiffusion)
	;

	// DitherEffect class
	class_<DitherEffect, base<Effect>>("DitherEffect")
		.constructor<>()
		.function("process", &DitherEffect::process)
		.function("setMode", &DitherEffect::setMode)
		.function("setBitDepth", &DitherEffect::setBitDepth)
		.function("setEnabled", &DitherEffect::setEnabled)
		.function("getMode", &DitherEffect::getMode)
		.function("getBitDepth", &DitherEffect::getBitDepth)
		.function("isEnabled", &DitherEffect::isEnabled)
		.function("reset", &DitherEffect::reset)
		.function("configureErrorDiffusion", &DitherEffect::configureErrorDiffusion)
		.function("configureSierraLite", &DitherEffect::configureSierraLite)
		.function("configureHighPassTPDF", &DitherEffect::configureHighPassTPDF)
		.function("configureShapedTPDF2ndOrder", &DitherEffect::configureShapedTPDF2ndOrder)
		.function("configureJJNErrorDiffusion", &DitherEffect::configureJJNErrorDiffusion)
	;

	// EffectChain class
	class_<EffectChain>("EffectChain")
		.constructor<>()
		.function("addEffect", &EffectChain::addEffect)
		.function("removeEffect", &EffectChain::removeEffect)
		.function("process", &EffectChain::process)
	;

	function("upcastView", &upcastView);
}
