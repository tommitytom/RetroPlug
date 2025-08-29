#include "application/GlfwNativeWindow.h"

#ifdef _MSC_VER
	#define __attribute__(x)
#endif

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include "core/Project.h"
#include "core/ProxySystem.h"
#include "foundation/DataBuffer.h"
#include "ui/RetroPlugView.h"
#include "lsdj/Ram.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"
#include "sameboy/Constants.h"

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

void setupWasmFs() {
	//backend_t opfs = wasmfs_create_opfs_backend();
	//spdlog::info("Created OPFS backend");
	//int err = wasmfs_create_directory("/opfs", 0777, opfs);
	//spdlog::info("Created OPFS directory: {}", err == 0 ? "success" : "failed");
}

EMSCRIPTEN_BINDINGS(retroPlug) {
	function("setupWasmFs", &setupWasmFs);

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



	function("upcastView", &upcastView);
}
