local MESEN_DIR = "thirdparty/Mesen2"

local m = {}

function m.include()
	includedirs { MESEN_DIR, MESEN_DIR .. "/Core" }
end

function m.source()
	m.include()

	includedirs { MESEN_DIR .. "/Core" }

	files {
		MESEN_DIR .. "/Utilities/**.h",
		MESEN_DIR .. "/Utilities/**.h",
		--MESEN_DIR .. "/Utilities/**.cpp",
		MESEN_DIR .. "/Core/NES/**.h",
		MESEN_DIR .. "/Core/NES/**.cpp",
		--MESEN_DIR .. "/Core/Shared/**.h",
		--MESEN_DIR .. "/Core/Shared/**.cpp",
	}
end

function m.link()
	m.include()
	links { "mesen" }
end

function m.project()
	project "mesen"
		kind "StaticLib"
		m.source()
end

return m