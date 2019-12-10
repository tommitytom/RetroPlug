#include "LuaContext.h"

#include "plugs/SameBoyPlug.h"

#include <sol/sol.hpp>
#include "util/fs.h"

bool validateResult(const sol::protected_function_result& result, const char* error) {
	if (!result.valid()) {
		sol::error err = result;
		std::string what = err.what();
		std::cout << error << what << std::endl;
		return false;
	}

	return true;
}

void LuaContext::init(RetroPlug* plug, const std::string& path) {
	_path = path;
	_plug = plug;
	setup();
}

SameBoyPlugPtr LuaContext::addInstance(EmulatorType type) {
	sol::protected_function f = (*_state)["_addInstance"];
	sol::protected_function_result result = f(type);
	if (validateResult(result, "Failed to add instance: ")) {
		return result.get<SameBoyPlugPtr>();
	}

	return nullptr;
}

void LuaContext::removeInstance(size_t index) {
	sol::protected_function f = (*_state)["_removeInstance"];
	sol::protected_function_result result = f(index);
	validateResult(result, "Failed to remove instance: ");
}

void LuaContext::setActive(size_t idx) {
	sol::protected_function f = (*_state)["_setActive"];
	sol::protected_function_result result = f(idx);
	validateResult(result, "Failed to set active instance: ");
}

void LuaContext::update(float delta) {
	if (_reload) {
		reload();
		_reload = false;
	}

	if (!_haltFrameProcessing) {
		sol::protected_function f = (*_state)["_frame"];
		sol::protected_function_result result = f(delta);
		_haltFrameProcessing = !validateResult(result, "Failed to process frame: ");
	}
}

void LuaContext::loadRom(InstanceIndex idx, const std::string& path) {
	sol::protected_function f = (*_state)["_loadRom"];
	sol::protected_function_result result = f(idx, path);
	validateResult(result, "Failed to load rom: ");
}

bool LuaContext::onKey(const iplug::igraphics::IKeyPress& key, bool down) {
	sol::protected_function f = (*_state)["_onKey"];
	sol::protected_function_result result = f(key, down);
	if (validateResult(result, "Failed to process key press: ")) {
		return result.get<bool>(); 
	}
	
	return false;
}

void LuaContext::onPadButton(int button, bool down) {
	sol::protected_function f = (*_state)["_onPadButton"];
	sol::protected_function_result result = f(button, down);
	validateResult(result, "Failed to process button press: ");
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
	_state->open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);

	std::string packagePath = (*_state)["package"]["path"];
	(*_state)["package"]["path"] = (packagePath + ";" + _path + "/?.lua").c_str();

	_state->new_usertype<SameBoyPlug>("SameBoyPlug",
		"setButtonState", &SameBoyPlug::setButtonStateT,
		"getRomName", &SameBoyPlug::romName,
		"isActive", &SameBoyPlug::active
	);

	_state->new_usertype<RetroPlug>("RetroPlug",
		"addInstance", &RetroPlug::addInstance,
		"removeInstance", &RetroPlug::removeInstance,
		"setActiveInstance", &RetroPlug::setActive,
		"activeInstanceIdx", &RetroPlug::activeInstanceIdx,
		"getPlug", &RetroPlug::getPlug,
		"loadRom", &RetroPlug::loadRom
	);

	_state->new_usertype<iplug::igraphics::IKeyPress>("IKeyPress",
		"vk", &iplug::igraphics::IKeyPress::VK,
		"shift", &iplug::igraphics::IKeyPress::S,
		"ctrl", &iplug::igraphics::IKeyPress::C,
		"alt", &iplug::igraphics::IKeyPress::A
	);

	(*_state)["_model"].set(_plug);

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
	return validateResult(res, ("Failed to load " + path).c_str());
}

bool LuaContext::runScript(const std::string& script) {
	sol::protected_function_result res = _state->do_string(script);
	return validateResult(res, "Failed to run script: ");
}

bool LuaContext::requireComponent(const std::string& path) {
	return runScript("_loadComponent(\"" + path + "\")");
}
