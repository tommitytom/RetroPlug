local paths = dofile("../paths.lua")

local YOGA_DIR = paths.DEP_ROOT .. "yoga"

local m = {}

function m.include()
	includedirs { paths.DEP_ROOT }
end

function m.source()
	m.include()

	files {
		YOGA_DIR .. "/**.h",
		YOGA_DIR .. "/**.cpp"
	}
end

function m.link()
	m.include()
	links { "yoga" }
end

function m.project()
	project "yoga"
		kind "StaticLib"

		m.source()

		--filter "system:windows"
			--disablewarnings { "4334", "4098", "4244" }
end

return m
