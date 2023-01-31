local paths = dofile("../paths.lua")

local GLAD_DIR = paths.DEP_ROOT .. "glad"

local m = {}

function m.include()
	sysincludedirs { GLAD_DIR .. "/include" }
end

function m.source()
	m.include()

	files {
		GLAD_DIR .. "/src/**.c"
	}
end

function m.link()
	m.include()
	links { "glad" }
end

function m.project()
	project "glad"
		kind "StaticLib"

		m.source()
end

return m
