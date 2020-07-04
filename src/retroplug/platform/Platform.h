#pragma once

//#define FORCE_LUA_COMPILE
#if !defined(_DEBUG) || defined(FORCE_LUA_COMPILE)// || defined(VST2_API)
#define COMPILE_LUA_SCRIPTS
#endif