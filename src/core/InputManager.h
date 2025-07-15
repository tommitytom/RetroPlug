#pragma once

#include <filesystem>
#include <string>

#include <sol/forward.hpp>

#include "foundation/Input.h"
#include "foundation/ButtonStream.h"
#include "core/FileManager.h"

namespace rp {
	class System;

	enum class InputType {
		Key,
		Pad,
		COUNT
	};

	class InputManager {
	private:
		struct Config {
			std::string name;
			bool valid;
		};

		FileManager& _fileManager;
		std::filesystem::path _rootPath;
		std::string _selectedConfigs[(int)InputType::COUNT];
		sol::state* _lua = nullptr;
		std::vector<Config> _configs;

	public:
		InputManager(FileManager& fileManager);
		~InputManager();

		void load(const std::string& name, InputType type);

		int getSelectedIndex(InputType type) {
			auto selected = _selectedConfigs[(int)type].empty() ? "default.lua" : _selectedConfigs[(int)type];
			for (size_t i = 0; i < _configs.size(); ++i) {
				if (_configs[i].name == selected) {
					return (int)i;
				}
			}

			return -1;
		}

		const std::vector<Config>& getAvailableConfigs() const {
			return _configs;
		}

		bool processKey(fw::VirtualKey key, bool down, fw::ButtonStreamWriter& buttons, std::vector<std::string>& actions);

		bool processButton(fw::PadButtonType button, bool down, fw::ButtonStreamWriter& buttons, std::vector<std::string>& actions);

		bool isValid() const {
			return _lua != nullptr;
		}

	private:
		void reload();
	};
}
