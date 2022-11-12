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
elseif _ACTION == "xcode4" then
	PLATFORMS = { "x64" }
end

workspace "Framework"
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
	_MAIN_SCRIPT_DIR .. "/src/compiler.config.lua"
})

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

group "1 - Dependencies"
dep.allProjects()

group "2 - Framework"
projects.Foundation.project()
projects.Graphics.project()
projects.Ui.project()
projects.Audio.project()
projects.Application.project()
projects.Engine.project()

group "3 - Examples"

--projects.ExampleApplication.project("CanvasTest")
projects.ExampleApplication.project("PhysicsTest")
--projects.ExampleApplication.project("ShaderReload")
--projects.ExampleApplication.project("Solitaire")
--projects.ExampleApplication.project("UiDocking")
--projects.ExampleApplication.project("UiScaling")
projects.ExampleApplication.project("Whitney")
--projects.ExampleApplication.project("BasicScene")
projects.ExampleApplication.project("Granular")
projects.ExampleApplication.project("LuaUi")
group ""

projects.Tests.project()
