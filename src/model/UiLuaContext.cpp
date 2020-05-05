#include "UiLuaContext.h"

#include "plugs/SameBoyPlug.h"

#include <sol/sol.hpp>
#include "util/fs.h"
#include "util/DataBuffer.h"
#include "util/File.h"
#include "model/FileManager.h"
#include "model/AudioContextProxy.h"
#include "util/base64enc.h"
#include "util/base64dec.h"
#include "config/config.h"

#include "platform/Platform.h"
#include "LuaHelpers.h"
#include "platform/Logger.h"

#include "LibLsdjWrapper.h"

#include "view/Menu.h"

#ifdef COMPILE_LUA_SCRIPTS
#include "CompiledLua.h"
#endif

void UiLuaContext::init(AudioContextProxy* proxy, const std::string& path, const std::string& scriptPath) {
	_configPath = path;
	_scriptPath = scriptPath;
	_proxy = proxy;
	setup();
}

void UiLuaContext::update(float delta) {
	if (_reload) {
		reload();
		_reload = false;
	}

	if (!_haltFrameProcessing) {
		_haltFrameProcessing = !callFunc(_state, "_frame", delta);
	}
}

bool UiLuaContext::onKey(const iplug::IKeyPress& key, bool down) {
	bool res = false;
	callFuncRet(_state, "_onKey", res, key, down);
	return res;
}

void UiLuaContext::onDoubleClick(float x, float y, const iplug::igraphics::IMouseMod& mod) {
	callFunc(_state, "_onDoubleClick", x, y, mod);
}

void UiLuaContext::onMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) {
	callFunc(_state, "_onMouseDown", x, y, mod);
}

void UiLuaContext::onPadButton(int button, bool down) {
	callFunc(_state, "_onPadButton", button, down);
}

void UiLuaContext::onDrop(float x, float y, const char* str) {
	std::vector<std::string> paths = { str };
	callFunc(_state, "_onDrop", paths);
}

void UiLuaContext::onMenu(std::vector<Menu*>& menus) {
	callFunc(_state, "_onMenu", menus);
}

void UiLuaContext::onMenuResult(int id) {
	callFunc(_state, "_onMenuResult", id);
}

void UiLuaContext::reload() {
	shutdown();
	setup();
	_haltFrameProcessing = false;
}

void UiLuaContext::shutdown() {
	if (_state) {
		delete _state;
		_state = nullptr;
	}
}

bool UiLuaContext::getDialogRequest(DialogRequest& request) {
	if (_dialogRequest.type != DialogType::None) {
		request = _dialogRequest;
		return true;
	}

	return false;
}

void UiLuaContext::handleDialogCallback(const std::vector<std::string>& paths) {
	callFunc(_state, "_handleDialogCallback", paths);
	_dialogRequest = DialogRequest();
}

bool isNullPtr(const sol::object o) {
	if (o.get_type() == sol::type::lightuserdata || o.get_type() == sol::type::userdata) {
		void* p = o.as<void*>();
		if (p > 0) {
			return true;
		}
	}

	return false;
}

void UiLuaContext::setup() {
	consoleLogLine("------------------------------------------");

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

	s["isNullPtr"].set_function(isNullPtr);
	s["_requestDialog"].set_function([&](const DialogRequest& request) { _dialogRequest = request; });

	setupCommon(s);
	setupLsdj(s);

	s.create_named_table("base64",
		"encode", base64::encode,
		"encodeBuffer", base64::encodeBuffer,
		"decode", base64::decode,
		"decodeBuffer", base64::decodeBuffer
	);

	s.new_usertype<FetchStateResponse>("FetchStateResponse",
		"buffers", &FetchStateResponse::buffers,
		"sizes", &FetchStateResponse::sizes,
		"components", &FetchStateResponse::components
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

	s.new_usertype<SystemDesc>("SystemDesc",
		"idx", &SystemDesc::idx,
		"emulatorType", &SystemDesc::emulatorType,
		"state", &SystemDesc::state,
		"romName", &SystemDesc::romName,
		"romPath", &SystemDesc::romPath,
		"savPath", &SystemDesc::savPath,
		"sameBoySettings", &SystemDesc::sameBoySettings,
		"sourceRomData", &SystemDesc::sourceRomData,
		"patchedRomData", &SystemDesc::patchedRomData,
		"sourceSavData", &SystemDesc::sourceSavData,
		"patchedSavData", &SystemDesc::patchedSavData,
		"sourceStateData", &SystemDesc::sourceStateData,
		"fastBoot", &SystemDesc::fastBoot
	);

	s.new_usertype<AudioContextProxy>("AudioContextProxy",
		"setSystem", &AudioContextProxy::setSystem,
		"duplicateSystem", &AudioContextProxy::duplicateSystem,
		"getProject", &AudioContextProxy::getProject,
		"loadRom", &AudioContextProxy::loadRom
	);

	s.new_usertype<iplug::IKeyPress>("IKeyPress",
		"vk", &iplug::IKeyPress::VK,
		"shift", &iplug::IKeyPress::S,
		"ctrl", &iplug::IKeyPress::C,
		"alt", &iplug::IKeyPress::A
	);

	s.new_usertype<iplug::igraphics::IMouseMod>("IMouseMod",
		"left", &iplug::igraphics::IMouseMod::L,
		"right", &iplug::igraphics::IMouseMod::R,
		"shift", &iplug::igraphics::IMouseMod::S,
		"ctrl", &iplug::igraphics::IMouseMod::C,
		"alt", &iplug::igraphics::IMouseMod::A
	);

	s["LUA_MENU_ID_OFFSET"] = LUA_UI_MENU_ID_OFFSET;
	s["_proxy"].set(_proxy);

	if (!runScript(_state, "require('plug')")) {
		return;
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

	runFile(_state, _configPath + "/config.lua");
	runScript(_state, "_init()");
}
