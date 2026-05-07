// WARNING! THIS CODE IS GENERATED AND WILL BE OVERWRITTEN!

#pragma once

#include <vector>
#include <string_view>
#include <unordered_map>
#include <cstdint>
#include "compiler/LuaScriptData.h"

typedef struct lua_State lua_State;

namespace rp::CompiledScripts {

namespace config {
	int loader(lua_State* state);
	void getScriptNames(std::vector<std::string_view>& names);
	const LuaScriptData* getScript(std::string_view path);
	const ScriptLookup& getScriptLookup();
}

namespace utils {
	int loader(lua_State* state);
	void getScriptNames(std::vector<std::string_view>& names);
	const LuaScriptData* getScript(std::string_view path);
	const ScriptLookup& getScriptLookup();
}

}
