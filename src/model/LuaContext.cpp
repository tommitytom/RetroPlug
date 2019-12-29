#include "LuaContext.h"

#include "plugs/SameBoyPlug.h"

#include <sol/sol.hpp>
#include "util/fs.h"
#include "util/DataBuffer.h"
#include "util/File.h"
#include "model/FileManager.h"
#include "model/RetroPlugProxy.h"

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

SameBoyPlugPtr LuaContext::addInstance(EmulatorType type) {
	/*SameBoyPlugPtr res;
	callFuncRet(_state, "_addInstance", res, type);
	return res;*/
	return nullptr;
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
	callFunc(_state, "_loadRom", idx, path);
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

static void loadRom(const std::string& path) {

}


void LuaContext::setup() {
	std::cout << "------------------------------------------" << std::endl;

	_state = new sol::state();
	sol::state& s = *_state;

	s.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);

	std::string packagePath = s["package"]["path"];
	s["package"]["path"] = (packagePath + ";" + _path + "/?.lua").c_str();

	s.new_usertype<DataBuffer<char>>("DataBuffer",
		"get", &DataBuffer<char>::get,
		"set", &DataBuffer<char>::set,
		"slice", &DataBuffer<char>::slice,
		"toString", &DataBuffer<char>::toString,
		"hash", &DataBuffer<char>::hash
	);

	s.new_usertype<FileManager>("FileManager",
		"loadFile", &FileManager::loadFile
	);

	s.new_usertype<File>("File",
		"data", sol::readonly(&File::data),
		"checksum", sol::readonly(&File::checksum)
	);

	s.new_usertype<SameBoyPlug>("SameBoyPlug",
		"setButtonState", &SameBoyPlug::setButtonStateT,
		"getRomName", &SameBoyPlug::romName,
		"getRomPath", &SameBoyPlug::romPath,
		"isActive", &SameBoyPlug::active
	);

	s.new_usertype<RetroPlug>("RetroPlug",
		"addInstance", &RetroPlug::addInstance,
		"removeInstance", &RetroPlug::removeInstance,
		"setActiveInstance", &RetroPlug::setActive,
		"activeInstanceIdx", &RetroPlug::activeInstanceIdx,
		"getPlug", &RetroPlug::getPlug,
		"loadRom", &RetroPlug::loadRom,
		"fileManager", &RetroPlug::fileManager
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

	s.new_usertype<EmulatorInstanceDesc>("EmulatorInstanceDesc",
		"idx", &EmulatorInstanceDesc::idx,
		"type", &EmulatorInstanceDesc::type,
		"state", &EmulatorInstanceDesc::state,
		"romName", &EmulatorInstanceDesc::romName,
		"romPath", &EmulatorInstanceDesc::romPath,
		"sourceRomData", &EmulatorInstanceDesc::sourceRomData,
		"patchedRomData", &EmulatorInstanceDesc::patchedRomData
	);

	s.new_usertype<RetroPlugProxy>("RetroPlugProxy",
		"setInstance", &RetroPlugProxy::setInstance,
		"removeInstance", &RetroPlugProxy::removeInstance,
		"getInstance", &RetroPlugProxy::getInstance,
		"getInstances", &RetroPlugProxy::instances,
		"setActiveInstance", &RetroPlugProxy::setActive,
		"activeInstanceIdx", &RetroPlugProxy::activeIdx,
		"fileManager", &RetroPlugProxy::fileManager
	);

	s.new_usertype<iplug::igraphics::IKeyPress>("IKeyPress",
		"vk", &iplug::igraphics::IKeyPress::VK,
		"shift", &iplug::igraphics::IKeyPress::S,
		"ctrl", &iplug::igraphics::IKeyPress::C,
		"alt", &iplug::igraphics::IKeyPress::A
	);

	s["_model"].set(_plug);
	s["_proxy"].set(_proxy);

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
