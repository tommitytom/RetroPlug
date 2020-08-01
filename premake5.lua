if os.istarget "Windows" then
	require "vstudio"
	local p = premake;
	local vc = p.vstudio.vc2010;

	function disableFastUpToDateCheck(prj, cfg)
		vc.element("DisableFastUpToDateCheck", nil, "true")
	end

	p.override(vc.elements, "globalsCondition",
			function(oldfn, prj, cfg)
				local elements = oldfn(prj, cfg)
				elements = table.join(elements, {disableFastUpToDateCheck})
				return elements
			end)
end

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

	configuration {}

project "RetroPlug"
	kind "StaticLib"

	includedirs {
		"config",
		"resources",
		"src/retroplug",
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
	includedirs {
		"src/retroplug",
		"src/plugin",
		"thirdparty",
		"thirdparty/gainput/lib/include",
		"thirdparty/simplefilewatcher/include",
		"thirdparty/lua-5.3.5/src",
		"thirdparty/liblsdj/liblsdj/include/lsdj",
		"thirdparty/minizip",
		"thirdparty/sol"
	}

	files {
		"src/plugin/**.h",
		"src/plugin/**.cpp",
		"resources/dlls/**",
		"resources/fonts/**",
	}

	links {
		"RetroPlug",
		"lua",
		"simplefilewatcher",
		"minizip",
		"gainput",
	}

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
	includedirs { "src/compiler", "thirdparty/lua-5.3.5/src", "thirdparty/sol" }
	files { "src/compiler/**.h", "src/compiler/**.c", "src/compiler/**.cpp" }
	links { "lua" }
