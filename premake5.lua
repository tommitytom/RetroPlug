local util = dofile("thirdparty/Framework/premake/util.lua")
require("thirdparty/Framework/premake/emscripten")

newoption {
	trigger = "emscripten",
	description = "Build with emscripten"
}

util.disableFastUpToDateCheck({ "generator", "configure" })

workspace "RetroPlug"
	util.setupWorkspace()

util.createConfigureProject()
util.createGeneratorProject({
	_MAIN_SCRIPT_DIR .. "/src/compiler.config.lua",
	_MAIN_SCRIPT_DIR .. "/thirdparty/Framework/src/compiler.config.lua",
})

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
--fwProjects.Engine.project()

group "3 - Modules"
projects.Core.project()
projects.SameBoyPlug.project()
projects.RetroPlug.project()

group "4 - Applications"
projects.Application.project()
projects.Application.projectLivepp()
projects.Application.iplugProject()
projects.Application.iplugVst2()

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
