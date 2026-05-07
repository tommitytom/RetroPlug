local paths = dofile("../paths.lua")
local EFSW_DIR = paths.DEP_ROOT .. "efsw"

local m = {}

function m.include()
	includedirs { EFSW_DIR .. "/include" }
	includedirs { EFSW_DIR .. "/src" }
end

function m.source()
	m.include()

	files {
		EFSW_DIR .. "/include/**.h",
		EFSW_DIR .. "/src/efsw/*.cpp"
	}

	filter "system:windows"
		files {
			EFSW_DIR .. "/src/efsw/platform/win/*.cpp"
		}

	filter "system:not windows"
		files {
			EFSW_DIR .. "/src/efsw/platform/posix/*.cpp"
		}

	filter {}
end

function m.link()
	m.include()
	links { "efsw" }
end

function m.project()
	project "efsw"
		removeplatforms { "Emscripten" }
		kind "StaticLib"

		m.source()

		--filter "system:windows"
			--disablewarnings { "4334", "4098", "4244" }
end

return m
