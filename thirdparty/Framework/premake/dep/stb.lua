local paths = dofile("../paths.lua")

local STB_DIR = paths.DEP_ROOT .. "stb"

local m = {}

function m.include()
	includedirs { STB_DIR .. "/src" }
end

function m.source()
	m.include()

	files {
		STB_DIR .. "/**.h",
		STB_DIR .. "/**.c"
	}
end

function m.link()
	m.include()
	links { "stb" }
end

function m.project()
	project "stb"
		kind "StaticLib"

		m.source()

		--filter "system:windows"
			--disablewarnings { "4334", "4098", "4244" }
		--filter{}
end

return m
