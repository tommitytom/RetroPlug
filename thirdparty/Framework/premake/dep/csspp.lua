local paths = dofile("../paths.lua")

local CSSPP_DIR = paths.DEP_ROOT .. "csspp"

local m = {}

function m.include()
	includedirs { CSSPP_DIR, CSSPP_DIR .. "/csspp" }
end

function m.source()
	m.include()

	files {
		CSSPP_DIR .. "/csspp/**.h",
		CSSPP_DIR .. "/csspp/**.cpp"
	}

	excludes {
		--CSSPP_DIR .. "/csspp/csspp.*"
	}
end

function m.link()
	m.include()
	links { "csspp" }
end

function m.project()
	project "csspp"
		kind "StaticLib"

		m.source()

		filter "system:windows"
			disablewarnings {
				"4244", "4068"
			}

		filter {}
end

return m
