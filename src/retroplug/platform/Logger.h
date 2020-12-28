#pragma once

#ifdef WIN32
#include <windows.h>
#endif

#include <string>
#include <iostream>

#include <spdlog/spdlog.h>

enum LogLevels {
	Debug = 0,
	Info = 1,
	Warning = 2,
	Error = 3,
	Print = 4
};

static void consoleLog(LogLevels level, const std::string& msg) {
#ifdef WIN32
	OutputDebugStringA((msg + "\r\n").c_str());
#endif

	switch (level) {
		case LogLevels::Info: spdlog::info(msg); break;
		case LogLevels::Warning: spdlog::warn(msg); break;
		case LogLevels::Error: spdlog::error(msg); break;
		case LogLevels::Debug:
		default: spdlog::debug(msg); break;
	}
}
