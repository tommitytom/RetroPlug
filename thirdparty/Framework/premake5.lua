require "premake/emscripten"

local util = dofile("premake/util.lua")
local dep = dofile("premake/dep/index.lua")
local projects = dofile("premake/projects.lua")

newoption {
	trigger = "emscripten",
	description = "Build with emscripten"
}

util.disableFastUpToDateCheck({ "generator", "configure" })

workspace "Framework"
	util.setupWorkspace()

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
projects.ExampleApplication.project("StaticReflection")
--projects.ExampleApplication.project("ShaderReload")
--projects.ExampleApplication.project("Solitaire")
--projects.ExampleApplication.project("UiDocking")
--projects.ExampleApplication.project("UiScaling")
projects.ExampleApplication.project("Whitney")
--projects.ExampleApplication.project("BasicScene")
projects.ExampleApplication.project("Granular")
--projects.ExampleApplication.project("LuaUi")
group ""

projects.Tests.project()
