#include "InputManager.h"

#include <filesystem>
#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "foundation/Logger.h"
#include "foundation/SolUtil.h"
#include "foundation/StlUtil.h"
#include "foundation/generated/CompiledScripts.h"
#include "generated/CompiledScripts.h"
#include "core/System.h"

namespace rp {
	InputManager::InputManager(FileManager& fileManager) : _fileManager(fileManager) {
		_rootPath = FileManager::getContentPath() / "input";
		_selectedConfigs[(int)InputType::Key] = "default.lua";
		_selectedConfigs[(int)InputType::Pad] = "default.lua";

		fileManager.startWatch(
			_rootPath,
			[this](const std::string& path, Watch::Action action) {
				this->reload();
			}
		);

		reload();
	}

	InputManager::~InputManager() {
		if (_lua) {
			delete _lua;
		}
	}

	bool prepareState(sol::state& s) {
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

		sol::protected_function_result result = s.script("require('InputConfigParser')");
		if (!result.valid()) {
			spdlog::error("Failed to load input config parser: {}", result.get<sol::error>().what());
			return false;
		}

		return true;
	}

	bool loadInputConfig(sol::state& s, const std::string& path, InputType type) {
		sol::protected_function_result result = s.script(fmt::format("prepare('{}')", type == InputType::Key ? "key" : "pad"));
		if (!result.valid()) {
			spdlog::error("Failed to clean input config data: {}", result.get<sol::error>().what());
			return false;
		}

		result = s.do_file(path);
		if (!result.valid()) {
			spdlog::error("Failed to load input config file '{}': {}", path, result.get<sol::error>().what());
			return false;
		}

		return true;
	}

	void InputManager::reload() {
		_configs.clear();

		std::vector<std::string> validConfigs;
		for (const auto& entry : std::filesystem::directory_iterator(_rootPath)) {
			if (entry.is_regular_file() && entry.path().extension() == ".lua") {
				sol::state s;
				prepareState(s);

				bool valid = loadInputConfig(s, entry.path().string(), InputType::Key);
				std::string fileName = entry.path().filename().string();
				_configs.push_back({ fileName, valid });

				if (valid) {
					validConfigs.push_back(fileName);
				}
			}
		}

		if (validConfigs.empty()) {
			spdlog::warn("No input config files found in '{}'", _rootPath.string());
			return;
		}

		for (size_t i = 0; i < (size_t)InputType::COUNT; ++i) {
			const std::string config = _selectedConfigs[i];
			if (!fw::StlUtil::vectorContains(validConfigs, config)) {
				if (fw::StlUtil::vectorContains(validConfigs, std::string("default.lua"))) {
					_selectedConfigs[i] = "default.lua";
				} else {
					_selectedConfigs[i] = validConfigs[0]; // Use the first available config
				}
			}
		}

		sol::state* s = new sol::state();
		prepareState(*s);
		bool keyValid = loadInputConfig(*s, (_rootPath / _selectedConfigs[(int)InputType::Key]).string(), InputType::Key);
		bool padValid = loadInputConfig(*s, (_rootPath / _selectedConfigs[(int)InputType::Pad]).string(), InputType::Pad);

		sol::protected_function_result result = s->script("cleanData()");
		if (!result.valid()) {
			spdlog::error("Failed to clean input config data: {}", result.get<sol::error>().what());
			delete s;
			return;
		}
		
		if (keyValid || padValid) {
			if (_lua) {
				delete _lua;
			}

			_lua = s;
		}
	}

	void InputManager::load(const std::string& name, InputType type) {
		std::filesystem::path path = _rootPath / name;

		if (!std::filesystem::exists(path)) {
			spdlog::error("Input config file '{}' does not exist", path.string());
			return;
		}

		_selectedConfigs[(int)type] = name;

		reload();
	}

	bool InputManager::processButton(fw::PadButtonType button, bool down, fw::ButtonWriter& buttons, std::vector<std::string>& actions) {
		if (!isValid()) return false;
		sol::function func = (*_lua)["processButton"];
		if (func.valid()) {
			func(button, down, buttons, actions);
			return true;
		}
		return false;
	}

	bool InputManager::processKey(fw::VirtualKey key, bool down, fw::ButtonWriter& buttons, std::vector<std::string>& actions) {
		if (!isValid()) return false;
		sol::function func = (*_lua)["processKey"];
		if (func.valid()) {
			func(key, down, buttons, actions);
			return true;
		}
		return false;
	}
}
