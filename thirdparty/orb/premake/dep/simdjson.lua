local paths = dofile("../paths.lua")

local SIMDJSON_DIR = paths.DEP_ROOT .. "simdjson"

local m = {}

function m.include()
	includedirs { SIMDJSON_DIR .. "/include" }
end

function m.source()
	m.include()

	files {
		SIMDJSON_DIR .. "/*.h",
		SIMDJSON_DIR .. "/*.cpp"
	}
end

function m.link()
	m.include()
	links { "simdjson" }
end

function m.project()
	project "simdjson"
		kind "StaticLib"

		m.source()

		--filter "system:windows"
			--disablewarnings { "4334", "4098", "4244" }
end

return m
