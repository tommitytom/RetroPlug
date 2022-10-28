local util = dofile("thirdparty/Framework/premake/util.lua")

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
elseif _ACTION == "xcode4" then
	PLATFORMS = { "x64" }
end

workspace "RetroPlug"
	location("build/" .. buildFolder)
	platforms(PLATFORMS)
	characterset "MBCS"
	cppdialect "C++20"
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
			["MACOSX_DEPLOYMENT_TARGET"] = "10.15",
			--["CODE_SIGN_IDENTITY"] = "",
			--["PROVISIONING_PROFILE_SPECIFIER"] = "",
			--["PRODUCT_BUNDLE_IDENTIFIER"] = "com.tommitytom.app.RetroPlug"
		};

		buildoptions {
			"-mmacosx-version-min=10.15"
		}

		linkoptions {
			"-mmacosx-version-min=10.15"
		}

	filter { "system:windows", "options:not emscripten" }
		cppdialect "C++latest"
		defines { "RP_WINDOWS", "_CRT_SECURE_NO_WARNINGS", "_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING", "_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING" }
		disablewarnings { 4834 }
		buildoptions { "/Zc:__cplusplus" }

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
util.createGeneratorProject({
	_MAIN_SCRIPT_DIR .. "/src/compiler.config.lua",
	_MAIN_SCRIPT_DIR .. "/thirdparty/Framework/src/compiler.config.lua",
})



_ROOT_PATH = "thirdparty/Framework/"
local fwProjects = dofile("thirdparty/Framework/premake/projects.lua")
local fwDeps = dofile("thirdparty/Framework/premake/dep/index.lua")
local projects = dofile("premake/projects.lua")
local deps = dofile("premake/dep/index.lua")

group "1 - Dependencies"
fwDeps.allProjects()
deps.allProjects()

group "2 - Framework"
fwProjects.Foundation.project()
fwProjects.Graphics.project()
fwProjects.Ui.project()
fwProjects.Audio.project()
fwProjects.Application.project()
fwProjects.Engine.project()

group "3 - Modules"
projects.Core.project()
projects.SameBoyPlug.project()
projects.RetroPlug.project()

group "4 - Applications"
projects.Application.project()
projects.Application.projectLivepp()

if _OPTIONS["emscripten"] == nil then
	group "5 - Utils"
		project "ScriptCompiler"
			kind "ConsoleApp"
			sysincludedirs { "thirdparty/Framework/thirdparty", "thirdparty/Framework/thirdparty/lua/src" }
			includedirs { "thirdparty/Framework/src/compiler" }
			files { "thirdparty/Framework/src/compiler/**.h", "thirdparty/Framework/src/compiler/**.c", "thirdparty/Framework/src/compiler/**.cpp" }

			links { "lua" }

			filter { "system:linux" }
				links { "pthread" }
end
