local LUA_DIR = "../thirdparty/lua-5.3.5/src"

project "lua"
	kind "StaticLib"
	language "C"

	includedirs { LUA_DIR }
	files { LUA_DIR .. "/**.h", LUA_DIR .. "/**.c" }
	excludes { LUA_DIR .. "/lua.c", LUA_DIR .. "/luac.c" }