#include "AudioLuaContext.h"

#include <sol/sol.hpp>
#include "platform/Logger.h"
#include "platform/Platform.h"
#include "LuaHelpers.h"
#include "config.h"
#include "plugs/SameBoyPlug.h"
#include "model/ProcessingContext.h"
#include "util/fs.h"
#include "platform/Menu.h"
#include "luawrapper/Wrappers.h"
#include "luawrapper/generated/CompiledScripts.h"

AudioLuaContext::AudioLuaContext(const std::string& configPath, const std::string& scriptPath) {
	_configPath = configPath;
	_scriptPath = scriptPath;
}

void AudioLuaContext::init(ProcessingContext* ctx, TimeInfo* timeInfo, double sampleRate) {
	_context = ctx;
	_timeInfo = timeInfo;
	_sampleRate = sampleRate;

	setup();
}

bool AudioLuaContext::setup() {
	consoleLogLine("");
	consoleLogLine("------------------------------------------");
	consoleLogLine("Initializing audio lua context");

	_state = new sol::state();
	sol::state& s = *_state;

	s.open_libraries(	sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, 
						sol::lib::math, sol::lib::debug, sol::lib::io	);

	std::string packagePath = s["package"]["path"];
	packagePath += ";" + _configPath + "/?.lua";

#ifdef COMPILE_LUA_SCRIPTS
	consoleLogLine("Using precompiled lua scripts");
	s.add_package_loader(CompiledScripts::common::loader);
	s.add_package_loader(CompiledScripts::audio::loader);
#else
	consoleLogLine("Loading lua scripts from disk");
	packagePath += ";" + _scriptPath + "/common/?.lua";
	packagePath += ";" + _scriptPath + "/audio/?.lua";
#endif

	s["package"]["path"] = packagePath;

	luawrappers::registerCommon(s);

	s.new_usertype<ProcessingContext>("ProcessingContext",
		"getSettings", &ProcessingContext::getSettings,
		"getInstance", &ProcessingContext::getInstance,
		"getButtonPresses", &ProcessingContext::getButtonPresses
	);

	s.new_usertype<SameBoyPlugDesc>("SameBoyPlug",
		"romName", &SameBoyPlugDesc::romName
	);

	s.new_usertype<SameBoyPlug>("SameBoyPlug",
		"sendSerialByte", &SameBoyPlug::sendSerialByte,
		"getDesc", &SameBoyPlug::getDesc
	);

	s.new_usertype<TimeInfo>("TimeInfo",
		"ppqPos", &TimeInfo::mPPQPos,
		"tempo", &TimeInfo::mTempo,
		"samplePos", &TimeInfo::mSamplePos,
		"ppqPos", &TimeInfo::mPPQPos,
		"lastBar", &TimeInfo::mLastBar,
		"cycleStart", &TimeInfo::mCycleStart,
		"cycleEnd", &TimeInfo::mCycleEnd,

		"numerator", &TimeInfo::mNumerator,
		"denominator", &TimeInfo::mDenominator,

		"transportIsRunning", &TimeInfo::mTransportIsRunning,
		"transportLoopEnabled", &TimeInfo::mTransportLoopEnabled
	);

	s["LUA_MENU_ID_OFFSET"] = LUA_AUDIO_MENU_ID_OFFSET;

	if (!runScript(s, "require('main')")) {
		return false;
	}

	if (!callFuncRet(s, "_getController", _controller)) {
		return false;
	}

	consoleLogLine("Looking for components...");

#ifdef COMPILE_LUA_SCRIPTS
	std::vector<std::string_view> names;
	CompiledScripts::audio::getScriptNames(names);
	loadComponentsFromBinary(s, names);
#else
	loadComponentsFromFile(s, _scriptPath + "/audio/components/");
#endif

	consoleLogLine("Finished loading components");

	// Set up the lua context
	if (!callFunc(_controller, "setup", _context, _timeInfo)) {
		consoleLogLine("Failed to setup view");
	}

	// Load the users config settings
	// TODO: This should probably happen outside of this class since it may be used by the 
	// audio lua context too.
	std::string configPath = _configPath + "/config.lua";
	if (!callFunc(_controller, "loadConfigFromPath", configPath)) {
		consoleLogLine("Failed to load config from " + configPath);
		//assert(false);
	}

	if (!callFunc(_controller, "initProject")) {
		consoleLogLine("Failed to setup project");
	}

	loadInputMaps(_controller, _configPath + "/input");

	consoleLogLine("------------------------------------------");
	consoleLogLine("");

	_valid = true;
	return true;
}

void AudioLuaContext::addInstance(SystemIndex idx, SameBoyPlugPtr instance, const std::string& componentState) {
	callFunc(_controller, "addInstance", idx, instance, componentState);
}

void AudioLuaContext::duplicateInstance(SystemIndex sourceIdx, SystemIndex targetIdx, SameBoyPlugPtr instance) {
	callFunc(_controller, "duplicateInstance", sourceIdx, targetIdx, instance);
}

void AudioLuaContext::removeInstance(SystemIndex idx) {
	callFunc(_controller, "removeInstance", idx);
}

void AudioLuaContext::setActive(SystemIndex idx) {
	callFunc(_controller, "setActive", idx);
}

void AudioLuaContext::update(int frameCount) {
	callFunc(_controller, "update", frameCount);
}

void AudioLuaContext::closeProject() {
	callFunc(_controller, "closeProject");
}

void AudioLuaContext::onMidi(int offset, int status, int data1, int data2) {
	callFunc(_controller, "onMidi", offset, status, data1, data2);
}

void AudioLuaContext::onMidiClock(int button, bool down) {

}

void AudioLuaContext::onMenu(SystemIndex idx, std::vector<Menu*>& menus) {
	callFunc(_controller, "onMenu", idx, menus);
}

void AudioLuaContext::onMenuResult(int id) {
	callFunc(_controller, "onMenuResult", id);
}

std::string AudioLuaContext::serializeInstance(SystemIndex index) {
	std::string target;
	callFuncRet(_controller, "serializeInstance", target, index);
	return target;
}

std::string AudioLuaContext::serializeInstances() {
	std::string target;
	callFuncRet(_controller, "serializeInstances", target);
	return target;
}

void AudioLuaContext::deserializeInstances(const std::string& data) {
	callFunc(_controller, "deserializeInstances", data);
}

void AudioLuaContext::deserializeComponents(const std::array<std::string, 4>& components) {
	for (size_t i = 0; i < components.size(); ++i) {
		if (!components[i].empty()) {
			callFunc(_controller, "deserializeInstance", i, "hello");
		}
	}
}

/*void AudioLuaContext::reload() {
	shutdown();
	setup();
	_haltFrameProcessing = false;
}*/

void AudioLuaContext::shutdown() {
	if (_state) {
		_controller = sol::table();
		delete _state;
		_state = nullptr;
	}
}
