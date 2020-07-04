local iplug2 = require("thirdparty/iPlug2/lua/iplug2").init()

iplug2.workspace "RetroPlug"
	platforms { "x64" }
	characterset "MBCS"
	cppdialect "C++latest"

	configuration { "Debug" }
		libdirs { "thirdparty/lib/debug_x64" }

	configuration { "Release" }
		libdirs { "thirdparty/lib/release_x64" }

	configuration { "Tracer" }
		libdirs { "thirdparty/lib/release_x64" }

local function retroplugProject()
	dependson { "ScriptCompiler" }

	includedirs {
		"src",
		"thirdparty",
		"thirdparty/gainput/lib/include",
		"thirdparty/simplefilewatcher/include",
		"thirdparty/lua-5.3.5/src",
		"thirdparty/liblsdj/liblsdj/include/lsdj",
		"thirdparty/minizip",
		"thirdparty/sol"
	}

	files {
		"thirdparty/liblsdj/liblsdj/include/lsdj/**.h",
		"thirdparty/liblsdj/liblsdj/src/**.c",

		"resources/dlls/**",
		"resources/fonts/**",

		"src/**.h",
		"src/**.c",
		"src/**.cpp"
	}

	links {
		"lua",
		"simplefilewatcher",
		"minizip",
		"gainput",
	}

	configuration { "windows" }
		links { "xinput" }

	filter { "files:src/luawrapper/**" }
		buildoptions { "/bigobj" }
end

group "Targets"
iplug2.project.app(retroplugProject)
iplug2.project.vst2(retroplugProject)
iplug2.project.vst3(retroplugProject)

group "Utils"
project "ScriptCompiler"
	kind "ConsoleApp"
	includedirs { "thirdparty/lua-5.3.5/src" }
	files { "compiler/main.cpp" }
	links { "lua" }
