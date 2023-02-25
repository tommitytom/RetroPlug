local paths = dofile("../paths.lua")
local SFW_DIR = paths.DEP_ROOT .. "simplefilewatcher"

local m = {}

function m.include()
	includedirs { SFW_DIR .. "/include" }
end

function m.source()
	m.include()

	files {
		SFW_DIR .. "/include/**.h",
		SFW_DIR .. "/source/**.cpp"
	}
end

function m.link()
	m.include()
	links { "simplefilewatcher" }
end

function m.project()
	project "simplefilewatcher"
		removeplatforms { "Emscripten" }
		kind "StaticLib"

		m.source()

		--filter "system:windows"
			--disablewarnings { "4334", "4098", "4244" }
end

return m
