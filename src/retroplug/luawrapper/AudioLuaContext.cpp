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
	setup();
}

void AudioLuaContext::init(ProcessingContext* ctx, TimeInfo* timeInfo, double sampleRate) {
	(*_state)["_model"].set(ctx);
	(*_state)["_timeInfo"].set(timeInfo);
	(*_state)["_sampleRate"].set(sampleRate);
	runScript(*_state, "_init()");
}

void AudioLuaContext::setup() {
	consoleLogLine("------------------------------------------");
	consoleLogLine("Initializing audio lua context");

	_state = new sol::state();
	sol::state& s = *_state;

	s.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);

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

	if (_timeInfo) {
		s["_timeInfo"].set(_timeInfo);
	}

	if (!runScript(s, "require('main')")) {
		return;
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

	runFile(s, _configPath + "/input/default.lua");
}

void AudioLuaContext::addInstance(SystemIndex idx, SameBoyPlugPtr instance, const std::string& componentState) {
	callFunc(*_state, "_addInstance", idx, instance, componentState);
}

void AudioLuaContext::duplicateInstance(SystemIndex sourceIdx, SystemIndex targetIdx, SameBoyPlugPtr instance) {
	callFunc(*_state, "_duplicateInstance", sourceIdx, targetIdx, instance);
}

void AudioLuaContext::removeInstance(SystemIndex idx) {
	callFunc(*_state, "_removeInstance", idx);
}

void AudioLuaContext::setActive(SystemIndex idx) {
	callFunc(*_state, "_setActive", idx);
}

void AudioLuaContext::update(int frameCount) {
	callFunc(*_state, "_update", frameCount);
}

void AudioLuaContext::closeProject() {
	callFunc(*_state, "_closeProject");
}

void AudioLuaContext::onMidi(int offset, int status, int data1, int data2) {
	callFunc(*_state, "_onMidi", offset, status, data1, data2);
}

void AudioLuaContext::onMidiClock(int button, bool down) {

}

void AudioLuaContext::onMenu(SystemIndex idx, std::vector<Menu*>& menus) {
	callFunc(*_state, "_onMenu", idx, menus);
}

void AudioLuaContext::onMenuResult(int id) {
	callFunc(*_state, "_onMenuResult", id);
}

std::string AudioLuaContext::serializeInstances() {
	std::string target;
	callFuncRet(*_state, "_serializeInstances", target);
	return target;
}

std::string AudioLuaContext::serializeInstance(SystemIndex index) {
	std::string target;
	callFuncRet(*_state, "_serializeInstance", target, index);
	return target;
}

void AudioLuaContext::deserializeInstances(const std::string& data) {
	callFunc(*_state, "_deserializeInstances", data);
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
