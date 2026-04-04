#include "Logger.h"

#ifdef FW_OS_WINDOWS
#include <windows.h>
#endif

#include <spdlog/spdlog.h>

namespace orb {
	void consoleLog(LogLevels level, const std::string& msg) {
#ifdef FW_OS_WINDOWS
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
}
