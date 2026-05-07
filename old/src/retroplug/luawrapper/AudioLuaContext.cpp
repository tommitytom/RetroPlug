#include "AudioLuaContext.h"

#include <sol/sol.hpp>

#include "platform/Logger.h"
#include "platform/Platform.h"
#include "platform/Menu.h"
#include "util/fs.h"
#include "model/ProcessingContext.h"
#include "plugs/SameBoyPlug.h"
#include "luawrapper/Wrappers.h"
#include "luawrapper/generated/CompiledScripts.h"
#include "LuaHelpers.h"
#include "config.h"

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
	spdlog::info("");
	spdlog::info("------------------------------------------");
	spdlog::info("Initializing audio lua context");

	_state = new sol::state();
	sol::state& s = *_state;

	s.open_libraries(	sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, 
						sol::lib::math, sol::lib::debug, sol::lib::io	);

	std::string packagePath = s["package"]["path"];
	packagePath += ";" + _configPath + "/?.lua";

#ifdef COMPILE_LUA_SCRIPTS
	spdlog::info("Using precompiled lua scripts");
	s.add_package_loader(CompiledScripts::common::loader);
	s.add_package_loader(CompiledScripts::audio::loader);
#else
	spdlog::info("Loading lua scripts from disk");
	packagePath += ";" + _scriptPath + "/common/?.lua";
	packagePath += ";" + _scriptPath + "/audio/?.lua";
#endif

	s["package"]["path"] = packagePath;

	luawrappers::registerCommon(s);

	s.new_usertype<ProcessingContext>("ProcessingContext",
		"getSettings", &ProcessingContext::getSettings,
		"getSystem", &ProcessingContext::getSystem,
		"getButtonPresses", &ProcessingContext::getButtonPresses
	);

	s.new_usertype<SameBoyPlugDesc>("SameBoyPlugDesc",
		"romName", &SameBoyPlugDesc::romName
	);

	s.new_usertype<SameBoyPlug>("SameBoyPlug",
		"sendSerialByte", &SameBoyPlug::sendSerialByte,
		"getDesc", &SameBoyPlug::getDesc,
		"getSramData", &SameBoyPlug::getSramData,
		"hashSram", [](SameBoyPlug& plug, size_t start, size_t size) {
			return (uint32_t)plug.hashSram(start, size);
		}
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

	spdlog::info("Looking for components...");

#ifdef COMPILE_LUA_SCRIPTS
	std::vector<std::string_view> names;
	CompiledScripts::audio::getScriptNames(names);
	loadComponentsFromBinary(s, names);
#else
	loadComponentsFromFile(s, _scriptPath + "/audio/components/");
#endif

	spdlog::info("Finished loading components");

	// Set up the lua context
	if (!callFunc(_controller, "setup", _context, _timeInfo, _sampleRate)) {
		spdlog::info("Failed to setup view");
		return false;
	}

	// Load the users config settings
	// TODO: This should probably happen outside of this class since it may be used by the 
	// audio lua context too.
	std::string configPath = _configPath + "/config.lua";
	if (!callFunc(_controller, "loadConfigFromPath", configPath)) {
		spdlog::info("Failed to load config from " + configPath);
		return false;
		//assert(false);
	}

	if (!callFunc(_controller, "initProject")) {
		spdlog::info("Failed to setup project");
		return false;
	}

	loadInputMaps(_controller, _configPath + "/input");

	spdlog::info("------------------------------------------");
	spdlog::info("");

	_valid = true;
	return true;
}

void AudioLuaContext::addSystem(SystemIndex idx, SameBoyPlugPtr system, const std::string& componentState) {
	if (_valid) {
		callFunc(_controller, "addSystem", idx, system, componentState);
	}
}

void AudioLuaContext::duplicateSystem(SystemIndex sourceIdx, SystemIndex targetIdx, SameBoyPlugPtr system) {
	if (_valid) {
		callFunc(_controller, "duplicateSystem", sourceIdx, targetIdx, system);
	}
}

void AudioLuaContext::removeSystem(SystemIndex idx) {
	if (_valid) {
		callFunc(_controller, "removeSystem", idx);
	}
}

void AudioLuaContext::setActive(SystemIndex idx) {
	if (_valid) {
		callFunc(_controller, "setActive", idx);
	}
}

void AudioLuaContext::setSampleRate(double sampleRate) {
	if (_valid) {
		callFunc(_controller, "setSampleRate", sampleRate);
	}
}

void AudioLuaContext::update(int frameCount) {
	if (_valid) {
		callFunc(_controller, "update", frameCount);
	}
}

void AudioLuaContext::closeProject() {
	if (_valid) {
		callFunc(_controller, "closeProject");
	}
}

void AudioLuaContext::onMidi(int offset, int status, int data1, int data2) {
	if (_valid) {
		callFunc(_controller, "onMidi", offset, status, data1, data2);
	}
}

void AudioLuaContext::onMidiClock(int button, bool down) {

}

void AudioLuaContext::onMenu(SystemIndex idx, std::vector<Menu*>& menus) {
	if (_valid) {
		callFunc(_controller, "onMenu", idx, menus);
	}
}

void AudioLuaContext::onMenuResult(int id) {
	if (_valid) {
		callFunc(_controller, "onMenuResult", id);
	}
}

std::string AudioLuaContext::serializeSystem(SystemIndex index) {
	std::string target;
	if (_valid) {
		callFuncRet(_controller, "serializeSystem", target, index);
	}

	return target;
}

std::string AudioLuaContext::serializeSystems() {
	std::string target;
	if (_valid) {
		callFuncRet(_controller, "serializeSystems", target);
	}

	return target;
}

void AudioLuaContext::deserializeSystems(const std::string& data) {
	if (_valid) {
		callFunc(_controller, "deserializeSystems", data);
	}
}

void AudioLuaContext::deserializeComponents(const std::array<std::string, 4>& components) {
	if (_valid) {
		for (size_t i = 0; i < components.size(); ++i) {
			if (!components[i].empty()) {
				callFunc(_controller, "deserializeSystem", i, "hello");
			}
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
