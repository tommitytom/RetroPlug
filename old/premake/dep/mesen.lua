local MESEN_DIR = "thirdparty/Mesen2"

local m = {}

function m.include()
	includedirs { MESEN_DIR, MESEN_DIR .. "/Core" }
end

function m.source()
	m.include()

	includedirs { MESEN_DIR .. "/Core", MESEN_DIR .. "/Utilities"  }

	files {
		MESEN_DIR .. "/SevenZip/**.h",
		MESEN_DIR .. "/SevenZip/**.c",
		MESEN_DIR .. "/Lua/**.h",
		MESEN_DIR .. "/Lua/**.c",
		MESEN_DIR .. "/Utilities/**.h",
		MESEN_DIR .. "/Utilities/**.cpp",
		MESEN_DIR .. "/Utilities/**.c",
		MESEN_DIR .. "/Core/**.h",
		MESEN_DIR .. "/Core/**.cpp",
		--MESEN_DIR .. "/Core/NES/**.h",
		--MESEN_DIR .. "/Core/NES/**.cpp",
		--MESEN_DIR .. "/Core/Shared/**.h",
		--MESEN_DIR .. "/Core/Shared/**.cpp",
		--MESEN_DIR .. "/Core/Debugger/**.h",
		--MESEN_DIR .. "/Core/Debugger/**.cpp",
		--MESEN_DIR .. "/Core/Netplay/**.h",
		--MESEN_DIR .. "/Core/Netplay/**.cpp",
	}

	excludes {
		MESEN_DIR .. "/Lua/unixstream.c",
		MESEN_DIR .. "/Lua/serial.c",
		MESEN_DIR .. "/Lua/unixdgram.c",
		--MESEN_DIR .. "/Core/Shared/Movies/**.cpp",
	}
end

function m.link()
	m.include()
	links { "mesen" }

	filter { "system:windows" }
		links { "winmm" }

	filter {}
end

function m.project()
	project "mesen"
		cppdialect "C++17"
		kind "StaticLib"
		characterset "Unicode"
		m.source()
end

return m