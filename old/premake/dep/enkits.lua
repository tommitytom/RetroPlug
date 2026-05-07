local ENKITS_DIR = "thirdparty/enkiTS"

local m = {}

function m.include()
	includedirs { ENKITS_DIR .. "/src" }
end

function m.source()
	m.include()

	files {
		ENKITS_DIR .. "/src/TaskScheduler.h",
		ENKITS_DIR .. "/src/TaskScheduler.cpp"
	}
end

function m.link()
	m.include()
	links { "enkits" }
end

function m.project()
	project "enkits"
		kind "StaticLib"
		m.source()
end

return m