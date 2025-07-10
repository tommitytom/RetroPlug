#pragma once

#include <string>
#include <filesystem>
#include <sol/forward.hpp>
#include "foundation/Input.h"
#include "foundation/ButtonStream.h"

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

		bool processKey(fw::VirtualKey key, bool down, fw::ButtonWriter& buttons);

		bool processButton(fw::ButtonType button, bool down);

		bool isValid() const {
			return _valid;
		}
	};
}
