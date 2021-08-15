local config = require "config"
newoption {
	trigger = "emscripten",
	description = "Build with emscripten"
}

local util = dofile("scripts/util.lua")
local iplug2 = require("thirdparty/iPlug2/lua/iplug2").init()

util.disableFastUpToDateCheck({ "RetroPlug", "configure" })

iplug2.workspace "RetroPlug"
	platforms { "x86", "x64" }
	characterset "MBCS"
	cppdialect "C++17"

	defines {
		"NOMINMAX"
	}

	configuration { "emscripten" }
		defines { "RP_WEB" }

	filter "system:linux"
		defines { "RP_LINUX", "RP_POSIX" }

	filter "system:macosx"
		toolset "clang"
		defines { "RP_MACOS", "RP_POSIX" }

		defines {
			"MACOSX_DEPLOYMENT_TARGET=10.9",
			"MIN_SUPPORTED_MACOSX_DEPLOYMENT_TARGET=10.9"
		}

		xcodebuildsettings {
			["MACOSX_DEPLOYMENT_TARGET"] = "10.9",
			["CODE_SIGN_IDENTITY"] = "",
			["PROVISIONING_PROFILE_SPECIFIER"] = "",
			["PRODUCT_BUNDLE_IDENTIFIER"] = "com.tommitytom.app.RetroPlug"
		};

		buildoptions {
			"-mmacosx-version-min=10.9"
		}

		linkoptions {
			"-mmacosx-version-min=10.9"
		}

	configuration { "windows" }
		defines { "RP_WINDOWS" }
		cppdialect "C++latest"
		defines { "_CRT_SECURE_NO_WARNINGS" }

	configuration {}

project "RetroPlug"
	kind "StaticLib"
	dependson { "SameBoy", "ScriptCompiler" }

	includedirs {
		"config",
		"resources",
		"src",
		"src/retroplug"
	}

	defines {
		"IGRAPHICS_GL2",
		"IGRAPHICS_NANOVG"
	}

	sysincludedirs {
		"thirdparty",
		"thirdparty/gainput/lib/include",
		"thirdparty/simplefilewatcher/include",
		"thirdparty/lua-5.3.5/src",
		"thirdparty/liblsdj/liblsdj/include/lsdj",
		"thirdparty/minizip-ng",
		"thirdparty/spdlog/include",
		"thirdparty/sol",
		"thirdparty/SameBoy/Core",
		"thirdparty/xxhash"
	}

	files {
		"src/retroplug/microsmsg/**.h",
		"src/retroplug/**.h",
		"src/retroplug/**.c",
		"src/retroplug/**.cpp",
		"src/retroplug/**.lua"
	}

	configuration { "not emscripten" }
		prebuildcommands {
			"%{wks.location}/bin/x64/Release/ScriptCompiler ../../src/compiler.config.lua"
		}

	filter { "system:windows", "files:src/retroplug/luawrapper/**" }
		buildoptions { "/bigobj" }

	configuration { "Debug" }
		excludes {
			"src/retroplug/luawrapper/generated/CompiledScripts_common.cpp",
			"src/retroplug/luawrapper/generated/CompiledScripts_audio.cpp",
			"src/retroplug/luawrapper/generated/CompiledScripts_ui.cpp",
		}

	configuration { "macosx" }
		files {
			"src/retroplug/**.m",
			"src/retroplug/**.mm"
		}

		-- TODO: These are temporary and used for dialog creation
		sysincludedirs {
			"thirdparty/iPlug2/IGraphics",
			"thirdparty/iPlug2/IPlug",
			"thirdparty/iPlug2/WDL",
			"thirdparty/iPlug2/Dependencies/IGraphics/NanoSVG/src"
		}

	configuration { "windows" }
		disablewarnings { "4996", "4250", "4018", "4267", "4068", "4150" }
		defines {
			"RP_WINDOWS",
			"NOMINMAX",
			"WIN32",
			"WIN64",
			"_CRT_SECURE_NO_WARNINGS"
		}

local function retroplugProject()
	defines { "GB_INTERNAL", "GB_DISABLE_TIMEKEEPING" }

	sysincludedirs {
		"thirdparty",
		"thirdparty/gainput/lib/include",
		"thirdparty/simplefilewatcher/include",
		"thirdparty/lua-5.3.5/src",
		"thirdparty/liblsdj/liblsdj/include/lsdj",
		"thirdparty/minizip-ng",
		"thirdparty/spdlog/include",
		"thirdparty/sol",
		"thirdparty/SameBoy/Core",
		"thirdparty/xxhash"
		--"thirdparty/iPlug2/IGraphics"
	}

	includedirs {
		".",
		"src",
		"src/retroplug",
		"src/plugin"
	}

	files {
		"src/plugin/**.h",
		"src/plugin/**.cpp"
	}

	links {
		"RetroPlug",
		"SameBoy",
		"liblsdj",
		"lua",
		"minizip"
	}

	configuration { "not emscripten" }
		links {
			"simplefilewatcher",
			"gainput",
		}

	configuration { "Debug" }
		symbols "Full"

	configuration { "windows" }
		links { "xinput" }

	configuration { "macosx" }
		files {
			"resources/fonts/**",
			"resources/RetroPlug-macOS-MainMenu.xib",
			"resources/RetroPlug-macOS-Info.plist"
		}
		links { "z" }

	filter { "system:macosx", "files:resources/fonts/**" }
		buildaction "Embed"
end

dofile("scripts/configure.lua")

group "Targets"
	iplug2.project.app(retroplugProject)
	--iplug2.project.vst2(retroplugProject)
	--iplug2.project.vst3(retroplugProject)
	--iplug2.project.wam(retroplugProject)

if _OPTIONS["emscripten"] then
	local EMSDK_FLAGS = {
		"-s WASM=1",
		--"-s LLD_REPORT_UNDEFINED",
		[[-s EXPORTED_FUNCTIONS='["_retroplug_init"]']] ,
		[[-s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]']],
		"-s EXPORT_ES6=1",
		"-s MODULARIZE=1"
	}

	local EMSDK_DEBUG_FLAGS = {
		"-g",
		"-v",
		"-o debug/index.html"
	}

	local EMSDK_RELEASE_FLAGS = {
		"-s ELIMINATE_DUPLICATE_FUNCTIONS=1",
		--"--MINIMAL_RUNTIME",
		"-Oz",
		"-closure",
		"-o release/retroplug.js"
	}

	project "RetroPlug-web"
		kind "SharedLib"
		defines { "RP_WEB" }

		defines { "GB_INTERNAL", "GB_DISABLE_TIMEKEEPING" }

		sysincludedirs {
			"thirdparty",
			"thirdparty/gainput/lib/include",
			"thirdparty/simplefilewatcher/include",
			"thirdparty/lua-5.3.5/src",
			"thirdparty/liblsdj/liblsdj/include/lsdj",
			"thirdparty/minizip-ng",
			"thirdparty/spdlog/include",
			"thirdparty/sol",
			"thirdparty/SameBoy/Core",
			--"thirdparty/iPlug2/IGraphics"
		}

		includedirs {
			--".",
			"src",
			"src/retroplug",
			"src/plugin"
		}

		files {
			"src/retroplug/RetroPlugWrap.cpp"
		}

		links {
			"RetroPlug",
			"SameBoy",
			"liblsdj",
			"lua",
			"minizip"
		}

		filter { "configurations:Debug" }
			linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEBUG_FLAGS) }

		filter { "configurations:Release" }
			linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_RELEASE_FLAGS) }
end

if _OPTIONS["emscripten"] == nil then
	group "Utils"
		project "ScriptCompiler"
			kind "ConsoleApp"
			sysincludedirs { "thirdparty", "thirdparty/lua-5.3.5/src" }
			includedirs { "src/compiler" }
			files { "src/compiler/**.h", "src/compiler/**.c", "src/compiler/**.cpp" }
			links { "lua" }
end

if _ACTION ~= "xcode4" then
	group "Dependencies"
		dofile("scripts/sameboy.lua")
		dofile("scripts/lua.lua")
		dofile("scripts/minizip.lua")
		dofile("scripts/liblsdj.lua")

		if _OPTIONS["emscripten"] == nil then
			dofile("scripts/gainput.lua")
			dofile("scripts/simplefilewatcher.lua")
		end
end
