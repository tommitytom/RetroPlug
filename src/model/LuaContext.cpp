#include "LuaContext.h"

#include "plugs/SameBoyPlug.h"

#include <sol/sol.hpp>
#include "util/fs.h"
#include "util/DataBuffer.h"
#include "util/File.h"
#include "model/FileManager.h"
#include "model/RetroPlugProxy.h"
#include "util/base64enc.h"
#include "util/base64dec.h"
#include "config/config.h"

bool validateResult(const sol::protected_function_result& result, const std::string& prefix, const std::string& name = "") {
	if (!result.valid()) {
		sol::error err = result;
		std::string what = err.what();
		std::cout << prefix;
		if (!name.empty()) {
			std::cout << " " << name;
		}
		
		std::cout << ": " << what << std::endl;
		return false;
	}

	return true;
}

template <typename ...Args>
bool callFunc(sol::state* state, const char* name, Args&&... args) {
	sol::protected_function f = (*state)[name];
	sol::protected_function_result result = f(args...); // Use std::forward?
	return validateResult(result, "Failed to call", name);
}

template <typename ReturnType, typename ...Args>
bool callFuncRet(sol::state* state, const char* name, ReturnType& ret, Args&&... args) {
	sol::protected_function f = (*state)[name];
	sol::protected_function_result result = f(args...);
	if (validateResult(result, "Failed to call", name)) {
		ret = result.get<ReturnType>();
		return true;
	}

	return false;
}

void LuaContext::init(RetroPlug* plug, RetroPlugProxy* proxy, const std::string& path) {
	_path = path;
	_plug = plug;
	_proxy = proxy;
	setup();
}

void LuaContext::closeProject() {
	callFunc(_state, "_closeProject");
}

void LuaContext::loadProject(const std::string& path) {
	callFunc(_state, "_loadProject", path);
}

void LuaContext::saveProject(const FetchStateResponse& res) {
	callFunc(_state, "_saveProjectToFile", res);
}

void LuaContext::removeInstance(size_t index) {
	callFunc(_state, "_removeInstance", index);
}

void LuaContext::setActive(size_t idx) {
	callFunc(_state, "_setActive", idx);
}

void LuaContext::update(float delta) {
	if (_reload) {
		reload();
		_reload = false;
	}

	if (!_haltFrameProcessing) {
		_haltFrameProcessing = !callFunc(_state, "_frame", delta);
	}
}

void LuaContext::loadRom(InstanceIndex idx, const std::string& path) {
	callFunc(_state, "_loadRomAtPath", idx, path);
}

bool LuaContext::onKey(const iplug::igraphics::IKeyPress& key, bool down) {
	bool res = false;
	callFuncRet(_state, "_onKey", res, key, down);
	return res;
}

void LuaContext::onPadButton(int button, bool down) {
	callFunc(_state, "_onPadButton", button, down);
}

void LuaContext::onDrop(const char* str) {
	std::vector<std::string> paths = { str };
	callFunc(_state, "_onDrop", paths);
} 

void LuaContext::reload() {
	shutdown();
	setup();
	_haltFrameProcessing = false;
}

void LuaContext::shutdown() {
	if (_state) {
		delete _state;
		_state = nullptr;
	}
}

void LuaContext::setup() {
	std::cout << "------------------------------------------" << std::endl;

	_state = new sol::state();
	sol::state& s = *_state;

	s.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math, sol::lib::io);

	std::string packagePath = s["package"]["path"];
	s["package"]["path"] = (packagePath + ";" + _path + "/?.lua").c_str();

	s.create_named_table("base64",
		"encode", base64::encode,
		"encodeBuffer", base64::encodeBuffer,
		"decode", base64::decode,
		"decodeBuffer", base64::decodeBuffer
	);

	s.new_usertype<DataBuffer<char>>("DataBuffer",
		"get", &DataBuffer<char>::get,
		"set", &DataBuffer<char>::set,
		"slice", &DataBuffer<char>::slice,
		"toString", &DataBuffer<char>::toString,
		"hash", &DataBuffer<char>::hash
	);

	s.new_usertype<FetchStateResponse>("FetchStateResponse",
		"buffers", &FetchStateResponse::buffers,
		"sizes", &FetchStateResponse::sizes
	);

	s.new_usertype<FileManager>("FileManager",
		"loadFile", &FileManager::loadFile,
		"exists", &FileManager::exists
	);

	s.new_usertype<File>("File",
		"data", sol::readonly(&File::data),
		"checksum", sol::readonly(&File::checksum)
	);

	s.new_enum("EmulatorInstanceState",
		"Uninitialized", EmulatorInstanceState::Uninitialized,
		"Initialized", EmulatorInstanceState::Initialized,
		"RomMissing", EmulatorInstanceState::RomMissing,
		"Running", EmulatorInstanceState::Running
	);

	s.new_enum("EmulatorType",
		"Unknown", EmulatorType::Unknown,
		"Placeholder", EmulatorType::Placeholder,
		"SameBoy", EmulatorType::SameBoy
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

	s.new_enum("InstanceLayout",
		"Auto", InstanceLayout::Auto,
		"Column", InstanceLayout::Column,
		"Grid", InstanceLayout::Grid,
		"Row", InstanceLayout::Row
	);

	s.new_enum("SaveStateType",
		"Sram", SaveStateType::Sram,
		"State", SaveStateType::State
	);

	s.new_usertype<EmulatorInstanceDesc>("EmulatorInstanceDesc",
		"idx", &EmulatorInstanceDesc::idx,
		"emulatorType", &EmulatorInstanceDesc::emulatorType,
		"state", &EmulatorInstanceDesc::state,
		"romName", &EmulatorInstanceDesc::romName,
		"romPath", &EmulatorInstanceDesc::romPath,
		"savPath", &EmulatorInstanceDesc::savPath,
		"sourceRomData", &EmulatorInstanceDesc::sourceRomData,
		"patchedRomData", &EmulatorInstanceDesc::patchedRomData,
		"sourceSavData", &EmulatorInstanceDesc::sourceSavData,
		"patchedSavData", &EmulatorInstanceDesc::patchedSavData,
		"sourceStateData", &EmulatorInstanceDesc::sourceStateData,
		"fastBoot", &EmulatorInstanceDesc::fastBoot
	);

	s.new_usertype<Project>("Project",
		"path", &Project::path,
		"instances", &Project::instances,
		"settings", &Project::settings
	);

	s.new_usertype<Project::Settings>("ProjectSettings",
		"audioRouting", &Project::Settings::audioRouting,
		"midiRouting", &Project::Settings::midiRouting,
		"layout", &Project::Settings::layout,
		"zoom", &Project::Settings::zoom,
		"saveType", &Project::Settings::saveType
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

	s.new_usertype<RetroPlugProxy>("RetroPlugProxy",
		"setInstance", &RetroPlugProxy::setInstance,
		"removeInstance", &RetroPlugProxy::removeInstance,
		"getInstance", &RetroPlugProxy::getInstance,
		"getInstances", &RetroPlugProxy::instances,
		"setActiveInstance", &RetroPlugProxy::setActive,
		"activeInstanceIdx", &RetroPlugProxy::activeIdx,
		"fileManager", &RetroPlugProxy::fileManager,
		"buttons", &RetroPlugProxy::getButtonPresses,
		"closeProject", &RetroPlugProxy::closeProject,
		"getProject", &RetroPlugProxy::getProject,
		"updateSettings", &RetroPlugProxy::updateSettings
	);

	s.new_usertype<iplug::igraphics::IKeyPress>("IKeyPress",
		"vk", &iplug::igraphics::IKeyPress::VK,
		"shift", &iplug::igraphics::IKeyPress::S,
		"ctrl", &iplug::igraphics::IKeyPress::C,
		"alt", &iplug::igraphics::IKeyPress::A
	);

	s["_model"].set(_plug);
	s["_proxy"].set(_proxy);
	s["_RETROPLUG_VERSION"].set(PLUG_VERSION_STR);

	if (!runFile(_path + "/plug.lua")) {
		return;
	}

	std::cout << "Looking for components..." << std::endl;
	for (const auto& entry : fs::directory_iterator(_path + "/components/")) {
		fs::path p = entry.path();
		std::string name = p.replace_extension("").filename().string();
		std::cout << "Loading " << name << ".lua... ";
		requireComponent(name);
	}

	std::cout << "Finished loading components" << std::endl;

	runFile(_path + "/config.lua");
	runScript("_init()");
}

bool LuaContext::runFile(const std::string& path) {
	sol::protected_function_result res = _state->do_file(path);
	return validateResult(res, "Failed to load", path);
}

bool LuaContext::runScript(const std::string& script, const char* error) {
	sol::protected_function_result res = _state->do_string(script);
	if (error != nullptr) {
		return validateResult(res, error);
	} else {
		return validateResult(res, "Failed to run script", script);
	}
}

bool LuaContext::requireComponent(const std::string& path) {
	return runScript("_loadComponent(\"" + path + "\")", "Failed to load component");
}
