#include "AudioLuaContext.h"

#include <sol/sol.hpp>
#include "platform/Logger.h"
#include "LuaHelpers.h"
#include "config/config.h"
#include "plugs/SameBoyPlug.h"
#include "ProcessingContext.h"
#include "util/fs.h"
#include "view/Menu.h"

AudioLuaContext::AudioLuaContext(const std::string& configPath, const std::string& scriptPath) {
	_configPath = configPath;
	_scriptPath = scriptPath;
	setup();
}

void AudioLuaContext::init(ProcessingContext* ctx, iplug::ITimeInfo* timeInfo, double sampleRate) {
	(*_state)["_model"].set(ctx);
	(*_state)["_timeInfo"].set(timeInfo);
	(*_state)["_sampleRate"].set(sampleRate);
	runScript(_state, "_init()");
}

void AudioLuaContext::setup() {
	consoleLogLine("------------------------------------------");

	_state = new sol::state();
	sol::state& s = *_state;

	s.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);

	std::string packagePath = s["package"]["path"];
	packagePath += ";" + _configPath + "/?.lua";

//#ifdef COMPILE_LUA_SCRIPTS
	//consoleLogLine("Using precompiled lua scripts");
	//s.add_package_loader(compiledScriptLoader);
//#else
	consoleLogLine("Loading lua scripts from disk");
	packagePath += ";" + _scriptPath + "/common/?.lua";
	packagePath += ";" + _scriptPath + "/audio/?.lua";
//#endif

	s["package"]["path"] = packagePath;

	setupCommon(s);

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

	s.new_usertype<iplug::ITimeInfo>("TimeInfo",
		"ppqPos", &iplug::ITimeInfo::mPPQPos,
		"tempo", &iplug::ITimeInfo::mTempo,
		"samplePos", &iplug::ITimeInfo::mSamplePos,
		"ppqPos", &iplug::ITimeInfo::mPPQPos,
		"lastBar", &iplug::ITimeInfo::mLastBar,
		"cycleStart", &iplug::ITimeInfo::mCycleStart,
		"cycleEnd", &iplug::ITimeInfo::mCycleEnd,

		"numerator", &iplug::ITimeInfo::mNumerator,
		"denominator", &iplug::ITimeInfo::mDenominator,

		"transportIsRunning", &iplug::ITimeInfo::mTransportIsRunning,
		"transportLoopEnabled", &iplug::ITimeInfo::mTransportLoopEnabled
	);

	s["LUA_MENU_ID_OFFSET"] = LUA_AUDIO_MENU_ID_OFFSET;

	if (_timeInfo) {
		s["_timeInfo"].set(_timeInfo);
	}

	if (!runScript(_state, "require('main')")) {
		return;
	}

	consoleLogLine("Looking for components...");

/*#ifdef COMPILE_LUA_SCRIPTS
	const std::vector<const char*>& names = getScriptNames();
	for (size_t i = 0; i < names.size(); ++i) {
		std::string_view name = names[i];
		if (name.substr(0, 10) == "components") {
			consoleLog("Loading " + std::string(name) + "... ");
			requireComponent(_state, std::string(name));
		}
	}
#else*/
	for (const auto& entry : fs::directory_iterator(_scriptPath + "/audio/components/")) {
		if (!entry.is_directory()) {
			fs::path p = entry.path();
			std::string name = p.replace_extension("").filename().string();
			consoleLog("Loading " + name + ".lua... ");
			requireComponent(_state, "components." + name);
		}
	}
//#endif

	consoleLogLine("Finished loading components");

	runFile(_state, _configPath + "/config.lua");
}

void AudioLuaContext::addInstance(InstanceIndex idx, SameBoyPlugPtr instance, const std::string& componentState) {
	callFunc(_state, "_addInstance", idx, instance, componentState);
}

void AudioLuaContext::duplicateInstance(InstanceIndex sourceIdx, InstanceIndex targetIdx, SameBoyPlugPtr instance) {
	callFunc(_state, "_duplicateInstance", sourceIdx, targetIdx, instance);
}

void AudioLuaContext::removeInstance(InstanceIndex idx) {
	callFunc(_state, "_removeInstance", idx);
}

void AudioLuaContext::setActive(InstanceIndex idx) {
	callFunc(_state, "_setActive", idx);
}

void AudioLuaContext::update(int frameCount) {
	callFunc(_state, "_update", frameCount);
}

void AudioLuaContext::closeProject() {
	callFunc(_state, "_closeProject");
}

void AudioLuaContext::onMidi(int offset, int status, int data1, int data2) {
	callFunc(_state, "_onMidi", offset, status, data1, data2);
}

void AudioLuaContext::onMidiClock(int button, bool down) {

}

void AudioLuaContext::onMenu(InstanceIndex idx, std::vector<Menu*>& menus) {
	callFunc(_state, "_onMenu", idx, menus);
}

void AudioLuaContext::onMenuResult(int id) {
	callFunc(_state, "_onMenuResult", id);
}

std::string AudioLuaContext::serializeInstances() {
	std::string target;
	callFuncRet(_state, "_serializeInstances", target);
	return target;
}

std::string AudioLuaContext::serializeInstance(InstanceIndex index) {
	std::string target;
	callFuncRet(_state, "_serializeInstance", target, index);
	return target;
}

void AudioLuaContext::deserializeInstances(const std::string& data) {
	callFunc(_state, "_deserializeInstances", data);
}

void AudioLuaContext::reload() {
	shutdown();
	setup();
	_haltFrameProcessing = false;
}

void AudioLuaContext::shutdown() {
	if (_state) {
		delete _state;
		_state = nullptr;
	}
}
