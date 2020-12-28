local util = dofile("scripts/util.lua")
local iplug2 = require("thirdparty/iPlug2/lua/iplug2").init()

util.disableFastUpToDateCheck({ "RetroPlug" })

iplug2.workspace "RetroPlug"
	platforms { "x86", "x64" }
	characterset "MBCS"
	cppdialect "C++latest"

	configuration { "windows" }
		defines { "_CRT_SECURE_NO_WARNINGS" }

	configuration {}

project "RetroPlug"
	kind "StaticLib"
	dependson { "SameBoy", "ScriptCompiler" }

	includedirs {
		"config",
		"resources",
		"src",
		"src/retroplug",
		"thirdparty",
		"thirdparty/gainput/lib/include",
		"thirdparty/simplefilewatcher/include",
		"thirdparty/lua-5.3.5/src",
		"thirdparty/liblsdj/liblsdj/include/lsdj",
		"thirdparty/minizip",
		"thirdparty/spdlog/include",
		"thirdparty/sol",
		"thirdparty/SameBoy/Core"
	}

	files {
		"src/retroplug/**.h",
		"src/retroplug/**.c",
		"src/retroplug/**.cpp",
		"src/retroplug/**.lua"
	}

	filter { "files:src/retroplug/luawrapper/**" }
		buildoptions { "/bigobj" }

	configuration { "Debug" }
		excludes {
			"src/retroplug/luawrapper/generated/CompiledScripts_common.cpp",
			"src/retroplug/luawrapper/generated/CompiledScripts_audio.cpp",
			"src/retroplug/luawrapper/generated/CompiledScripts_ui.cpp",
		}

	configuration { "windows" }
		disablewarnings { "4996", "4250", "4018", "4267", "4068", "4150" }
		defines {
			"NOMINMAX",
			"WIN32",
			"WIN64",
			"_CRT_SECURE_NO_WARNINGS"
		}

	configuration { "windows" }
		prebuildcommands {
			"%{wks.location}/bin/x64/Release/ScriptCompiler.exe ../../src/compiler.config.lua"
		}

local function retroplugProject()
	defines { "GB_INTERNAL", "GB_DISABLE_TIMEKEEPING" }

	includedirs {
		"src",
		"src/retroplug",
		"src/plugin",
		"thirdparty",
		"thirdparty/gainput/lib/include",
		"thirdparty/simplefilewatcher/include",
		"thirdparty/lua-5.3.5/src",
		"thirdparty/liblsdj/liblsdj/include/lsdj",
		"thirdparty/minizip",
		"thirdparty/spdlog/include",
		"thirdparty/sol",
		"thirdparty/SameBoy/Core"
	}

	files {
		"src/plugin/**.h",
		"src/plugin/**.cpp",
		"resources/fonts/**",
	}

	links {
		"RetroPlug",
		"lua",
		"simplefilewatcher",
		"minizip",
		"gainput",
		"SameBoy",
		"liblsdj"
	}

	configuration { "Debug" }
		symbols "Full"

	configuration { "windows" }
		links { "xinput" }
end

group "Targets"
iplug2.project.app(retroplugProject)
iplug2.project.vst2(retroplugProject)
iplug2.project.vst3(retroplugProject)

group "Utils"
project "ScriptCompiler"
	kind "ConsoleApp"
	includedirs { "src/compiler", "thirdparty", "thirdparty/lua-5.3.5/src" }
	files { "src/compiler/**.h", "src/compiler/**.c", "src/compiler/**.cpp" }
	links { "lua" }

group "Dependencies"
	dofile("scripts/sameboy.lua")
	dofile("scripts/lua.lua")
	dofile("scripts/gainput.lua")
	dofile("scripts/minizip.lua")
	dofile("scripts/simplefilewatcher.lua")
	dofile("scripts/liblsdj.lua")
