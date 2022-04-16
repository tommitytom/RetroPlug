local R8BRAIN_DIR = "thirdparty/r8brain"

local m = {}

function m.include()
	sysincludedirs { R8BRAIN_DIR }
end

function m.source()
	m.include()

	files {
		R8BRAIN_DIR .. "/*.h",
		R8BRAIN_DIR .. "/r8bbase.cpp"
	}
end

function m.link()
	m.include()
	links { "r8brain" }
end

function m.project()
	project "r8brain"
		kind "StaticLib"

		m.source()
end

return m
