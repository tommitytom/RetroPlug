local paths = dofile("../paths.lua")

local BIN2H_DIR = paths.DEP_ROOT .. "bin2h"

local m = {}

function m.include()
	sysincludedirs { BIN2H_DIR .. "/include" }
end

function m.source()
	m.include()

	files {
		BIN2H_DIR .. "/main.cpp"
	}
end

function m.link()
	m.include()
	links { "bin2h" }
end

function m.project()
	project "bin2h"
		kind "ConsoleApp"

		m.source()
end

return m
