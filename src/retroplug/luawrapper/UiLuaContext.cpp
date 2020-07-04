#include "UiLuaContext.h"

#include <chrono>

#include "plugs/SameBoyPlug.h"

#include "util/fs.h"
#include "util/DataBuffer.h"
#include "util/File.h"
#include "model/FileManager.h"
#include "util/base64enc.h"
#include "util/base64dec.h"
#include "config.h"
#include "platform/Menu.h"

#include "platform/Platform.h"
#include "LuaHelpers.h"
#include "platform/Logger.h"
#include "Wrappers.h"

#ifdef COMPILE_LUA_SCRIPTS
#include "CompiledLua.h"
#endif

void UiLuaContext::init(AudioContextProxy* proxy, const std::string& path, const std::string& scriptPath) {
	_configPath = path;
	_scriptPath = scriptPath;
	_proxy = proxy;
	setup();
}

void UiLuaContext::update(double delta) {
	if (_reload) {
		reload();
		_reload = false;
	}

	if (!_haltFrameProcessing) {
		//_haltFrameProcessing = !callFunc(_state, "_frame", delta);
	}
}

bool UiLuaContext::onKey(VirtualKey key, bool down) {
	bool res = false;
	callFuncRet(_viewRoot, "onKey", res, key, down);
	return res;
}

void UiLuaContext::onDoubleClick(float x, float y) {
	callFunc(_viewRoot, "onDoubleClick", x, y);
}

void UiLuaContext::onMouseDown(float x, float y) {
	callFunc(_viewRoot, "onMouseDown", x, y);
}

void UiLuaContext::onPadButton(int button, bool down) {
	callFunc(_viewRoot, "onPadButton", button, down);
}

void UiLuaContext::onDrop(float x, float y, const char* str) {
	std::vector<std::string> paths = { str };
	callFunc(_viewRoot, "onDrop", x, y, paths);
}

void UiLuaContext::onMenu(std::vector<Menu*>& menus) {
	callFunc(_viewRoot, "onMenu", menus);
}

void UiLuaContext::onMenuResult(int id) {
	callFunc(_viewRoot, "onMenuResult", id);
}

void UiLuaContext::reload() {
	if (_valid) {
		callFunc(_viewRoot, "onReloadBegin");
	}
	
	shutdown();
	setup();

	if (_valid) {
		callFunc(_viewRoot, "onReloadEnd");
	}
	
	_haltFrameProcessing = !_valid;
}

void UiLuaContext::shutdown() {
	if (_state) {
		_viewRoot = sol::table();
		delete _state;
		_state = nullptr;
	}
}

void UiLuaContext::handleDialogCallback(const std::vector<std::string>& paths) {
	callFunc(_viewRoot, "onDialogResult", paths);
}

DataBufferPtr UiLuaContext::saveState() {
	DataBufferPtr buffer = std::make_shared<DataBuffer<char>>();
	callFunc(_viewRoot, "saveState", buffer);
	return buffer;
}

void UiLuaContext::loadState(DataBufferPtr buffer) {
	callFunc(_viewRoot, "loadState", buffer);
}

bool UiLuaContext::setup() {
	consoleLogLine("------------------------------------------");

	_valid = false;
	_state = new sol::state();
	sol::state& s = *_state;

	s.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math, sol::lib::debug, sol::lib::coroutine);

	std::string packagePath = s["package"]["path"];
	packagePath += ";" + _configPath + "/?.lua";

#ifdef COMPILE_LUA_SCRIPTS
	consoleLogLine("Using precompiled lua scripts");
	s.add_package_loader(compiledScriptLoader);
#else
	consoleLogLine("Loading lua scripts from disk");
	packagePath += ";" + _scriptPath + "/common/?.lua";
	packagePath += ";" + _scriptPath + "/ui/?.lua";
#endif

	s["package"]["path"] = packagePath;
	
	luawrappers::registerCommon(s);
	luawrappers::registerChrono(s);
	luawrappers::registerLsdj(s);
	luawrappers::registerZipp(s);
	luawrappers::registerRetroPlug(s);

	s.create_named_table("base64",
		"encode", base64::encode,
		"encodeBuffer", base64::encodeBuffer,
		"decode", base64::decode,
		"decodeBuffer", base64::decodeBuffer
	);

	s.new_usertype<FileManager>("FileManager",
		"loadFile", &FileManager::loadFile,
		"saveFile", &FileManager::saveFile,
		"saveTextFile", &FileManager::saveTextFile,
		"exists", &FileManager::exists
	);

	s.new_usertype<File>("File",
		"data", sol::readonly(&File::data),
		"checksum", sol::readonly(&File::checksum)
	);

	s.new_usertype<ViewWrapper>("ViewWrapper",
		"requestDialog", &ViewWrapper::requestDialog,
		"requestMenu", &ViewWrapper::requestMenu
	);

	s.create_named_table("nativeutil", 
		"mergeMenu", mergeMenu
	);

	s["LUA_MENU_ID_OFFSET"] = LUA_UI_MENU_ID_OFFSET;

	if (!runScript(_state, "require('main')")) {
		return false;
	}

	if (!callFuncRet(_state, "_getView", _viewRoot)) {
		return false;
	}

	consoleLogLine("Looking for components...");

#ifdef COMPILE_LUA_SCRIPTS
	const std::vector<const char*>& names = getScriptNames();
	for (size_t i = 0; i < names.size(); ++i) {
		std::string_view name = names[i];
		if (name.substr(0, 10) == "components") {
			consoleLog("Loading " + std::string(name) + "... ");
			requireComponent(_state, std::string(name));
		}
	}
#else
	for (const auto& entry : fs::directory_iterator(_scriptPath + "/ui/components/")) {
		if (!entry.is_directory()) {
			fs::path p = entry.path();
			std::string name = p.replace_extension("").filename().string();
			consoleLog("Loading " + name + ".lua... ");
			requireComponent(_state, "components." + name);
		}
	}
#endif

	consoleLogLine("Finished loading components");

	if (!runFile(_state, _configPath + "/config.lua")) {
		consoleLogLine("Failed to load user config");
	}

	if (!callFunc(_viewRoot, "setup", &_viewWrapper, _proxy)) {
		consoleLogLine("Failed to setup view");
	}

	_valid = true;
	return true;
}
