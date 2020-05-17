#include "UiLuaContext.h"

#include "plugs/SameBoyPlug.h"

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
		//_haltFrameProcessing = !callFunc(_state, "_frame", delta);
	}
}

bool UiLuaContext::onKey(const iplug::IKeyPress& key, bool down) {
	bool res = false;
	callFuncRet(_viewRoot, "onKey", res, key, down);
	return res;
}

void UiLuaContext::onDoubleClick(float x, float y, const iplug::igraphics::IMouseMod& mod) {
	callFunc(_viewRoot, "onDoubleClick", x, y, mod);
}

void UiLuaContext::onMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) {
	callFunc(_viewRoot, "onMouseDown", x, y, mod);
}

void UiLuaContext::onPadButton(int button, bool down) {
	callFunc(_viewRoot, "onPadButton", button, down);
}

void UiLuaContext::onDrop(float x, float y, const char* str) {
	std::vector<std::string> paths = { str };
	callFunc(_viewRoot, "onDrop", paths);
}

void UiLuaContext::onMenu(std::vector<Menu*>& menus) {
	callFunc(_viewRoot, "onMenu", menus);
}

void UiLuaContext::onMenuResult(int id) {
	callFunc(_viewRoot, "onMenuResult", id);
}

void UiLuaContext::reload() {
	shutdown();
	setup();
	_haltFrameProcessing = false;
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

	setupCommon(s);
	setupLsdj(s);

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

	s.new_usertype<SystemDesc>("SystemDesc",
		"new", sol::factories(
			[]() { return std::make_shared<SystemDesc>(); },
			[](const SystemDesc& other) { return std::make_shared<SystemDesc>(other); }
		),
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
		"fastBoot", &SystemDesc::fastBoot,
		"audioComponentState", &SystemDesc::audioComponentState,
		"area", &SystemDesc::area,
		"buttons", &SystemDesc::buttons,
		"clear", &SystemDesc::clear
	);

	s.new_usertype<AudioContextProxy>("AudioContextProxy",
		"setSystem", &AudioContextProxy::setSystem,
		"duplicateSystem", &AudioContextProxy::duplicateSystem,
		"getProject", &AudioContextProxy::getProject,
		"loadRom", &AudioContextProxy::loadRom,
		"getFileManager", &AudioContextProxy::getFileManager,
		"updateSettings", &AudioContextProxy::updateSettings,
		"removeSystem", &AudioContextProxy::removeSystem,
		"clearProject", &AudioContextProxy::clearProject,
		"resetSystem", &AudioContextProxy::resetSystem,
		"fetchSystemStates", &AudioContextProxy::fetchSystemStates
	);

	s.new_usertype<ViewWrapper>("ViewWrapper",
		"requestDialog", &ViewWrapper::requestDialog,
		"requestMenu", &ViewWrapper::requestMenu
	);

	s.new_usertype<Rect>("Rect",
		sol::constructors<Rect(), Rect(float, float, float, float)>(),
		"x", &Rect::x,
		"y", &Rect::y,
		"w", &Rect::w,
		"h", &Rect::h,
		"right", &Rect::right,
		"bottom", &Rect::bottom,
		"contains", &Rect::contains
	);

	s.new_usertype<Point>("Point",
		sol::constructors<Point(), Point(float, float)>(),
		"x", &Point::x,
		"y", &Point::y
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

	s.new_usertype<FetchStateResponse>("FetchStateResponse",
		"buffers", &FetchStateResponse::buffers,
		"components", &FetchStateResponse::components,
		"sizes", &FetchStateResponse::sizes,
		"type", &FetchStateResponse::type
	);

	s["LUA_MENU_ID_OFFSET"] = LUA_UI_MENU_ID_OFFSET;
	//s["_proxy"].set(_proxy);

	if (!runScript(_state, "require('main')")) {
		return;
	}

	if (!callFuncRet(_state, "_getView", _viewRoot)) {
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

	if (!runFile(_state, _configPath + "/config.lua")) {
		consoleLogLine("Failed to load user config");
	}

	if (!callFunc(_viewRoot, "setup", &_viewWrapper, _proxy)) {
		consoleLogLine("Failed to setup view");
	}
}
