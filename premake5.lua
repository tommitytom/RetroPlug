local util = dofile("thirdparty/orb/premake/util.lua")
require("thirdparty/orb/premake/emscripten")

newoption {
	trigger = "emscripten",
	description = "Build with emscripten"
}

newoption {
	trigger = "admin",
	description = "Writes VST plugins to protected folders on windows (requires admin privileges)",
}

util.disableFastUpToDateCheck({ "generator", "configure" })

workspace "RetroPlug"
	util.setupWorkspace()

util.createConfigureProject()
util.createGeneratorProject({
	_MAIN_SCRIPT_DIR .. "/src/compiler.config.lua",
	_MAIN_SCRIPT_DIR .. "/thirdparty/orb/src/compiler.config.lua",
})

local fwProjects = dofile("thirdparty/orb/premake/projects.lua")
local fwDeps = dofile("thirdparty/orb/premake/dep/index.lua")
local projects = dofile("premake/projects.lua")
local deps = dofile("premake/dep/index.lua")

group "1 - Dependencies"
fwDeps.allProjects()
deps.allProjects()

group "2 - orb"
fwProjects.Foundation.project()
fwProjects.Graphics.project()
fwProjects.Ui.project()
fwProjects.Audio.project()
fwProjects.Application.project()
--fwProjects.Engine.project()

group "3 - Modules"
projects.Core.project()
projects.SameBoyPlug.project()
projects.MesenPlug.project()
projects.RetroPlug.project()

group "4 - Applications"

projects.Cli.project()

fwProjects.Application.create({
	version = "0.6.0",
	name = "RetroPlug",
	namespace = "rp::",
	header = "RetroPlugApplication.h",
	author = "tommitytom",
	url = "https://retroplug.io",
	email = "hello@retroplug.io",
	copyright = "Tom Yaxley",

	targets = {
		"vst2",
		"vst3",
		"standalone",
		"standalone-livepp",
		"standalone-iplug",
		"au",
		"aax",
		"web",
		"clap"
	},

	audio = {
		inputs = 0,
		outputs = 2,
		midiIn = true,
		midiOut = false,
		latency = 0,
		stateChunks = true,
	},

	graphics = {
		width = 320,
		height = 288,
		fps = 60,
		vsync = true
	},

	plugin = {
		authorId = "tmtt",
		type = "synth",
		sharedResources = false,
	}
}, function()
	projects.RetroPlug.link()
	filter { "platforms:Emscripten" }
		files { "src/EmscriptenBindings.cpp" }
	filter{}
end)

--[[projects.Application.project()
projects.Application.projectLivepp()
projects.Application.iplugProject()
projects.Application.iplugVst2()]]

if _OPTIONS["emscripten"] == nil then
	group "5 - Utils"
		project "ScriptCompiler"
			kind "ConsoleApp"
			includedirs { "thirdparty/orb/thirdparty", "thirdparty/orb/thirdparty/lua/src" }
			includedirs { "thirdparty/orb/src/compiler" }
			files { "thirdparty/orb/src/compiler/**.h", "thirdparty/orb/src/compiler/**.c", "thirdparty/orb/src/compiler/**.cpp" }

			links { "lua" }

			filter { "system:linux" }
				links { "pthread" }
end
