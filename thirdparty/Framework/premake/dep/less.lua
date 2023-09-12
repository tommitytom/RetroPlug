local paths = dofile("../paths.lua")

local LESS_DIR = paths.DEP_ROOT .. "clessc"

local m = {}

function m.include()
	includedirs { LESS_DIR .. "/libless/include" }
end

function m.source()
	m.include()

	files {
		LESS_DIR .. "/libless/include/**.h",
		LESS_DIR .. "/libless/src/**.cpp"
	}
end

function m.link()
	m.include()
	links { "less" }
end

function m.project()
	project "less"
		kind "StaticLib"

		m.source()

		filter "system:windows"
			disablewarnings {
				"4244"
			}

		filter {}
end

return m
