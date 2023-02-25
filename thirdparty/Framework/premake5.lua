require "premake/emscripten"

local util = dofile("premake/util.lua")
local dep = dofile("premake/dep/index.lua")
local projects = dofile("premake/projects.lua")

newoption {
	trigger = "emscripten",
	description = "Build with emscripten"
}

util.disableFastUpToDateCheck({ "generator", "configure" })

local buildFolder = _ACTION

if _OPTIONS["web"] then
	buildFolder = "emscripten"
end

local PLATFORMS = { "x86", "x64" }

if _ACTION == "gmake2" then
	table.insert(PLATFORMS, "Emscripten")
elseif _ACTION == "xcode4" then
	PLATFORMS = { "x64" }
end


workspace "Framework"
	location("build/" .. buildFolder)

	configurations { "Debug", "Development", "Release", "Debug-ASAN", "Development-ASAN", "Release-ASAN", "Release-Profiling" }
	platforms (PLATFORMS)
	flags { "MultiProcessorCompile" }
	language "C++"
	characterset "MBCS"
	cppdialect "c++20"
	vectorextensions "SSE2"

	filter "configurations:Debug*"
		defines { "_DEBUG", "FW_DEBUG" }
		optimize "Off"
		symbols "On"
	filter "configurations:Development*"
		defines { "NDEBUG", "FW_DEVELOPMENT" }
		optimize "Full"
		symbols "On"
		inlining "Explicit"
		intrinsics "Off"
		omitframepointer "Off"
	filter "configurations:Release*"
		defines { "NDEBUG", "FW_RELEASE" }
		-- intrinsics "On"
		optimize "Full"
	filter "configurations:Release-Profiling"
		defines { "FW_PROFILING" }
		symbols "On" -- For tracy
	filter { "action:vs*", "configurations:Debug* or Development*" }
		symbols "Full"
	filter { "action:vs*", "platforms:Emscripten" }
		toolset "emcc"

	filter "platforms:x86"
		architecture "x86"
	filter "platforms:x64"
		architecture "x64"
	filter "platforms:Emscripten"
		architecture "x86"
		defines { "FW_PLATFORM_WEB", "FW_COMPILER_CLANG" }

	filter { "action:vs*", "configurations:*ASAN" }
		buildoptions { "/fsanitize=address" }
		editandcontinue "Off"
		flags { "NoIncrementalLink" }

	filter { "action:gmake2", "configurations:*ASAN" }
		buildoptions { "-fsanitize=address" }
		linkoptions { "-fsanitize=address" }

	filter { "system:linux", "platforms:not Emscripten" }
		toolset "clang"
		defines { "FW_OS_LINUX" }

	filter { "system:linux" }
		defines { "FW_COMPILER_CLANG" }
		buildoptions { "-Wfatal-errors" }
		disablewarnings { "macro-redefined", "switch", "nonportable-include-path" }

	filter { "system:windows", "platforms:not Emscripten" }
		defines { "FW_OS_WINDOWS" }

	filter { "action:vs*", "platforms:not Emscripten" }
		defines {
			"FW_COMPILER_MSVC",
			"NOMINMAX",
			"_CRT_SECURE_NO_WARNINGS",
			"_SILENCE_CXX20_CISO646_REMOVED_WARNING",
			"_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING"
		}

		staticruntime "on"
		libdirs {  "dep/lib" }
		buildoptions { "/Zc:__cplusplus" }

	filter { "platforms:Emscripten" }
		defines { "FW_PLATFORM_WEB" }
		disablewarnings { "macro-redefined", "switch" }
		buildoptions { "-matomics", "-mbulk-memory", "-msimd128" }

	filter { "platforms:not Emscripten" }
		defines { "FW_PLATFORM_STANDALONE" }

		filter { "system:macosx", "options:not emscripten" }
		defines { "FW_OS_MACOS", "FW_OS_POSIX" }

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

	filter {}

util.createConfigureProject()
util.createGeneratorProject({
	_MAIN_SCRIPT_DIR .. "/src/compiler.config.lua"
})

if _OPTIONS["emscripten"] == nil then
	group "Utils"
		project "ScriptCompiler"
			removeplatforms { "Emscripten" }
			kind "ConsoleApp"

			includedirs { "thirdparty", "thirdparty/lua/src" }
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

projects.ExampleApplication.project("CanvasTest")
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
