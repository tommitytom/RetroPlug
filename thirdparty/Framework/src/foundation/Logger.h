#pragma once

#include <string>

namespace fw {
	enum LogLevels {
		Debug = 0,
		Info = 1,
		Warning = 2,
		Error = 3,
		Print = 4
	};

	void consoleLog(LogLevels level, const std::string& msg);
}
