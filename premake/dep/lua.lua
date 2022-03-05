local LUA_DIR = "thirdparty/lua"

local m = {}

function m.include()
	includedirs { LUA_DIR .. "/src" }

	filter { "system:linux" }
		defines { "LUA_USE_POSIX" }
		buildoptions { "-Wno-string-plus-int" }
	filter {}
end

function m.source()
	m.include()

	files {
		LUA_DIR .. "/src/**.h",
		LUA_DIR .. "/src/**.c"
	}

	excludes {
		LUA_DIR .. "/src/lua.c",
		LUA_DIR .. "/src/luac.c"
	}
end

function m.link()
	m.include()
	links { "lua" }
end

function m.project()
	project "lua"
		kind "StaticLib"

		m.source()

		--configuration { "windows" }
			--disablewarnings { "4334", "4098", "4244" }
end

return m
