#include "LuaHelpers.h"

#include "util/fs.h"
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

void loadComponentsFromFile(sol::state& state, const std::string path) {
	for (const auto& entry : fs::directory_iterator(path)) {
		if (!entry.is_directory()) {
			fs::path p = entry.path();
			if (p.extension() == ".lua") {
				std::string name = p.replace_extension("").filename().string();
				consoleLog("Loading " + name + ".lua... ");
				requireComponent(state, "components." + name);
			}
		}
	}
}

void loadComponentsFromBinary(sol::state& state, const std::vector<std::string_view>& names) {
	for (size_t i = 0; i < names.size(); ++i) {
		std::string_view name = names[i];
		if (name.substr(0, 11) == LUA_COMPONENT_PREFIX && name.find_first_of(".", LUA_COMPONENT_PREFIX.size()) == std::string::npos) {
			consoleLog("Loading " + std::string(name) + "... ");
			requireComponent(state, std::string(name));
		}
	}
}

void loadInputMaps(sol::table& table, const std::string path) {
	for (const auto& entry : fs::directory_iterator(path)) {
		if (!entry.is_directory()) {
			fs::path p = entry.path();
			std::string name = p.filename().string();
			consoleLog("Loading " + name + "... ");

			if (!callFunc(table, "loadInputConfig", p.string())) {
				consoleLogLine("Failed to load user input config");
			}
		}
	}
}
