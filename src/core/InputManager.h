#pragma once

#include <string>
#include <filesystem>
#include <sol/forward.hpp>
#include "foundation/Input.h"

namespace rp {
	class System;

	class InputManager {
	private:
		std::filesystem::path _rootPath;
		sol::state* _lua = nullptr;
		bool _valid = false;

	public:
		InputManager(const std::filesystem::path& rootPath): _rootPath(rootPath) {}
		~InputManager();

		void load(const std::string& name);

		std::vector<std::string> getAvailableConfigs() const;

		bool processKey(fw::VirtualKey key, bool down, System* system);

		bool processGlobalKey(fw::VirtualKey key, bool down);

		bool processButton(fw::ButtonType button, bool down);

		bool processGlobalButton(fw::ButtonType button, bool down);

		bool isValid() const {
			return _valid;
		}
	};
}
