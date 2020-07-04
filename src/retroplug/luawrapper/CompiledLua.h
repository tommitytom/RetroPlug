#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <vector>
#include "platform/Platform.h"

int compiledScriptLoader(lua_State* state);
const std::vector<const char*>& getScriptNames();
