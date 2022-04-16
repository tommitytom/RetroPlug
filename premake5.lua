local util = dofile("premake/util.lua")
local dep = dofile("premake/dep/index.lua")
local projects = dofile("premake/projects.lua")

newoption {
	trigger = "emscripten",
	description = "Build with emscripten"
}

util.disableFastUpToDateCheck({ "generator", "configure" })

local buildFolder = _ACTION

local PLATFORMS = { "x86", "x64" }
if _OPTIONS["emscripten"] ~= nil then
	PLATFORMS = { "x86" }
	buildFolder = "emscripten"
end

workspace "RetroPlugAll"
	location("build/" .. buildFolder)
	platforms(PLATFORMS)
	characterset "MBCS"
	cppdialect "C++2a"
	flags { "MultiProcessorCompile" }

	configurations { "Debug", "Release", "Tracer" }

	defines { "NOMINMAX" }

	filter "configurations:Debug"
		defines { "RP_DEBUG", "DEBUG", "_DEBUG" }
		symbols "Full"

	filter "configurations:Release"
		defines { "RP_RELEASE", "NDEBUG" }
		optimize "On"
		--flags { "LinkTimeOptimization" }

	filter "configurations:Tracer"
		defines { "RP_TRACER", "NDEBUG", "TRACER_BUILD" }
		optimize "On"

	filter { "options:emscripten" }
		defines { "RP_WEB" }
		buildoptions { "-matomics", "-mbulk-memory", "-fexceptions" }

	filter { "system:linux", "options:not emscripten" }
		defines { "RP_LINUX", "RP_POSIX" }
		buildoptions { "-Wno-unused-function" }

	filter { "system:macosx", "options:not emscripten" }
		defines { "RP_MACOS", "RP_POSIX" }

		xcodebuildsettings {
			["MACOSX_DEPLOYMENT_TARGET"] = "10.9",
			--["CODE_SIGN_IDENTITY"] = "",
			--["PROVISIONING_PROFILE_SPECIFIER"] = "",
			--["PRODUCT_BUNDLE_IDENTIFIER"] = "com.tommitytom.app.RetroPlug"
		};

		buildoptions {
			"-mmacosx-version-min=10.9"
		}

		linkoptions {
			"-mmacosx-version-min=10.9"
		}

	filter { "system:windows", "options:not emscripten" }
		cppdialect "C++latest"
		defines { "RP_WINDOWS", "_CRT_SECURE_NO_WARNINGS", "_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING" }
		disablewarnings { "4834" }

	filter { "options:emscripten" }
		buildoptions { "-matomics", "-mbulk-memory" }
		disablewarnings {
			"deprecated-enum-float-conversion",
			"deprecated-volatile"
		}

	filter { "system:linux" }
		disablewarnings {
			"switch",
			"unused-result",
			"unused-function",
		}

	filter {}

util.createConfigureProject()
util.createGeneratorProject()

if _OPTIONS["emscripten"] == nil then
	group "Utils"
		project "ScriptCompiler"
			kind "ConsoleApp"
			sysincludedirs { "thirdparty", "thirdparty/lua/src" }
			includedirs { "src/compiler" }
			files { "src/compiler/**.h", "src/compiler/**.c", "src/compiler/**.cpp" }

			links { "lua" }

			filter { "system:linux" }
				links { "pthread" }
end

group "Dependencies"
dep.allProjects()
group ""

projects.RetroPlug.project()
projects.Application.project()
projects.Application.projectLivepp()
projects.ExampleApplication.project()
projects.ExampleApplication.projectLivepp()
projects.Tests.project()
