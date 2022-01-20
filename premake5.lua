local util = dofile("premake/util.lua")
local dep = dofile("premake/dep/index.lua")
local projects = dofile("premake/projects.lua")

newoption {
	trigger = "emscripten",
	description = "Build with emscripten"
}

util.disableFastUpToDateCheck({ "configure" })

local PLATFORMS = { "x86", "x64" }
if _OPTIONS["emscripten"] ~= nil then
	PLATFORMS = { "x86" }
end

workspace "RetroPlugAll"
	location("build/" .. _ACTION)
	platforms(PLATFORMS)
	characterset "MBCS"
	cppdialect "C++2a"
	flags { "MultiProcessorCompile" }

	configurations { "Debug", "Release", "Tracer" }

	defines { "NOMINMAX" }

	configuration { "Debug" }
		defines { "RP_DEBUG", "DEBUG", "_DEBUG" }
		symbols "Full"
		--symbols "On"

	configuration { "Release" }
		defines { "RP_RELEASE", "NDEBUG" }
		optimize "On"
		--flags { "LinkTimeOptimization" }

	configuration { "Tracer" }
		defines { "RP_TRACER", "NDEBUG", "TRACER_BUILD" }
		optimize "On"

	filter { "options:emscripten" }
		defines { "RP_WEB" }
		buildoptions { "-matomics", "-mbulk-memory" }

	filter { "system:linux", "options:not emscripten" }
		defines { "RP_LINUX", "RP_POSIX" }
		buildoptions { "-Wunused-function" }

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

if _OPTIONS["emscripten"] == nil then
	group "Utils"
		project "ScriptCompiler"
			kind "ConsoleApp"
			sysincludedirs { "thirdparty", "thirdparty/lua/src" }
			includedirs { "src/compiler" }
			files { "src/compiler/**.h", "src/compiler/**.c", "src/compiler/**.cpp" }
			links { "lua", "pthread" }
end

group "Dependencies"
dep.allProjects()
group ""

projects.RetroPlug.project()
projects.Application.project()
projects.Application.projectLivepp()
projects.ExampleApplication.project()
projects.ExampleApplication.projectLivepp()

--dep.SameBoy.project()

--[[
local SAMEBOY_DIR = "thirdparty/SameBoy"

project "ClangTest"
	kind "StaticLib"
	toolset "clang"
	language "C"

	--dep.SameBoy.include()

	defines { "GB_INTERNAL", "GB_DISABLE_TIMEKEEPING" }
	sysincludedirs { SAMEBOY_DIR .. "/Core" }

	includedirs {
		"src",
		"generated"
	}

	files {
		SAMEBOY_DIR .. "/Core/**.h",
		SAMEBOY_DIR .. "/Core/**.c",
		"src/clangtest/**.h",
		"src/clangtest/**.cpp"
	}

	filter { "system:windows" }
			includedirs { SAMEBOY_DIR .. "/Windows" }

project "TestApp"
	kind "ConsoleApp"

	defines { "GB_INTERNAL", "GB_DISABLE_TIMEKEEPING" }
	sysincludedirs { SAMEBOY_DIR .. "/Core" }

	includedirs {
		"src",
		"generated"
	}

	files {
		"src/test/**.h",
		"src/test/**.cpp"
	}

	links { "ClangTest" }
]]