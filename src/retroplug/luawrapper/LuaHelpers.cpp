#include "LuaHelpers.h"

#include "platform/Logger.h"

bool validateResult(const sol::protected_function_result& result, const std::string& prefix, const std::string& name) {
	if (!result.valid()) {
		sol::error err = result;
		std::string what = err.what();
		consoleLog(prefix);
		if (!name.empty()) {
			consoleLog(" " + name);
		}

		consoleLogLine(": " + what);
		return false;
	}

	return true;
}
