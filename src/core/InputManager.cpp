#include "InputManager.h"

#include <filesystem>
#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "foundation/Logger.h"
#include "foundation/SolUtil.h"
#include "foundation/generated/CompiledScripts.h"
#include "generated/CompiledScripts.h"
#include "core/System.h"

namespace rp {
	InputManager::~InputManager() {
		if (_lua) {
			delete _lua;
		}
	}

	void InputManager::load(const std::string& name) {
		std::filesystem::path path = _rootPath / name;

		if (!std::filesystem::exists(path)) {
			spdlog::error("Input config file '{}' does not exist", path.string());
			_valid = false;
			return;
		}
		
		if (_lua) {
			delete _lua;
		}

		_lua = new sol::state();
		sol::state& s = *_lua;

		s.open_libraries(sol::lib::base, sol::lib::package, sol::lib::debug, sol::lib::table, sol::lib::string, sol::lib::math);
		s.add_package_loader(fw::CompiledScripts::utils::loader);
		s.add_package_loader(rp::CompiledScripts::utils::loader);
		s["_consolePrint"].set_function(fw::consoleLog);

		s.new_enum<fw::ButtonType>("Button", fw::ButtonTypeUtil::Items);
		s.new_enum<fw::PadButtonType>("Pad", fw::PadButtonTypeUtil::Items);
		s.new_enum<fw::VirtualKey>("Key", fw::VirtualKeyUtil::Items);

		s.new_usertype<fw::ButtonWriter>("ButtonStream",
			"hold", &fw::ButtonWriter::hold,
			"release", &fw::ButtonWriter::release,
			"releaseAll", &fw::ButtonWriter::releaseAll,
			"delay", &fw::ButtonWriter::delay,
			"press", &fw::ButtonWriter::press,
			"holdDuration", &fw::ButtonWriter::holdDuration,
			"releaseDuration", &fw::ButtonWriter::releaseDuration,
			"releaseAllDuration", &fw::ButtonWriter::releaseAllDuration
		);

		s.script("require('InputConfigParser')");

		std::string data = fw::FsUtil::readTextFile(path);

		const sol::protected_function_result result = s.do_file(path.string());
		_valid = result.valid();

		if (!_valid) {
			spdlog::error("Failed to load input config file '{}': {}", path.string(), result.get<sol::error>().what());
			return;
		}

		s.script("cleanData()");
	}

	std::vector<std::string> InputManager::getAvailableConfigs() const {
		std::vector<std::string> configs;
		for (const auto& entry : std::filesystem::directory_iterator(_rootPath)) {
			if (entry.is_regular_file() && entry.path().extension() == ".lua") {
				configs.push_back(entry.path().filename().string());
			}
		}
		return configs;
	}

	bool InputManager::processButton(fw::ButtonType button, bool down) {
		if (!_valid) return false;
		sol::function func = (*_lua)["processButton"];
		if (func.valid()) {
			func(button, down);
			return true;
		}
		return false;
	}

	bool InputManager::processKey(fw::VirtualKey key, bool down, fw::ButtonWriter& buttons, std::vector<std::string>& actions) {
		if (!_valid) return false;
		sol::function func = (*_lua)["processKey"];
		if (func.valid()) {
			func(key, down, buttons, actions);
			return true;
		}
		return false;
	}
}
