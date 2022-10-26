#pragma once

#include <string_view>

std::string_view HEADER_CODE_TEMPLATE = R"(// WARNING! THIS CODE IS GENERATED AND WILL BE OVERWRITTEN!

#pragma once

#include <vector>
#include <string_view>
#include <unordered_map>
#include <cstdint>
#include "compiler/LuaScriptData.h"

typedef struct lua_State lua_State;

namespace CompiledScripts {

using ScriptLookup = std::unordered_map<std::string_view, LuaScriptData>;

)";

std::string_view HEADER_FUNCS_TEMPLATE = R"(
	int loader(lua_State* state);
	void getScriptNames(std::vector<std::string_view>& names);
	const LuaScriptData* getScript(std::string_view path);
	const ScriptLookup& getScriptLookup();
)";

std::string_view SOURCE_HEADER_TEMPLATE = R"(// WARNING! THIS CODE IS GENERATED AND WILL BE OVERWRITTEN!

#include "CompiledScripts.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace CompiledScripts::)";

std::string_view SOURCE_FOOTER_TEMPLATE = R"(
int loader(lua_State* L) {
	const char* name = lua_tostring(L, -1);
	const auto& found = _lookup.find(name);
	if (found != _lookup.end()) {
		luaL_loadbuffer(L, (const char*)found->second.data, found->second.size, name);
		return 1;
	}

	return 0;
}

void getScriptNames(std::vector<std::string_view>& names) {
	names.reserve(names.size() + _lookup.size());
	for (const auto& script : _lookup) {
		names.push_back(script.first);
	}
}

const LuaScriptData* getScript(std::string_view path) {
	const auto& found = _lookup.find(path);
	if (found != _lookup.end()) {
		return &found->second;
	}

	return nullptr;
}

const ScriptLookup& getScriptLookup() {
	return _lookup;
}

}
)";
