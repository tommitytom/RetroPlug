#pragma once

#include <string>

namespace rp {
	struct SystemPaths {
		std::string romPath;
		std::string sramPath;
		std::string statePath;
	};

	struct SystemSettings {
		bool includeRom = true;
		bool gameLink = false;
		bool reloadRomOnChange = true;
	};
}
