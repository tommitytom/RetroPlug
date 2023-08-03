local paths = dofile("paths.lua")
local dep = dofile(paths.SCRIPT_ROOT .. "dep/index.lua")
local iplug2 = dofile(paths.SCRIPT_ROOT .. "dep/iplug2.lua")
local util = dofile(paths.SCRIPT_ROOT .. "util.lua")
local emscripten = dofile(paths.SCRIPT_ROOT .. "emscripten.lua")

local m = {
	Foundation = {},
	Graphics = {},
	Ui = {},
	Audio = {},
	Engine = {},
	Application = {
		test2 = function ()

		end
	},
	ExampleApplication = {},
	Tests = {}
}

function m.Foundation.include()
	dependson { "configure" }

	includedirs {
		paths.DEP_ROOT,
		paths.DEP_ROOT .. "spdlog/include"
	}

	includedirs {
		"thirdparty",
		"thirdparty/spdlog/include",
		"thirdparty/sol",
	}

	includedirs {
		paths.SRC_ROOT,
		paths.GENERATED_ROOT,
		paths.RESOURCES_ROOT
	}

	dep.lua.include()
	dep.simplefilewatcher.include()
	dep.bgfx.compat()

	filter {}
end

function m.Foundation.link()
	m.Foundation.include()

	links { "Foundation" }

	dep.lua.link()
	dep.simplefilewatcher.link()
end

function m.Foundation.project()
	project "Foundation"
	kind "StaticLib"

	m.Foundation.include()

	files {
		paths.SRC_ROOT .. "foundation/**.h",
		paths.SRC_ROOT .. "foundation/**.cpp",
		paths.SRC_ROOT .. "foundation/generated/*.h",
		paths.SRC_ROOT .. "foundation/generated/*_%{cfg.platform}.cpp",
	}

	dep.bgfx.compat()
	util.liveppCompat()
end


function m.Graphics.include()
	dependson { "configure" }

	m.Foundation.include()
	dep.bgfx.include()
	dep.glfw.include()
	dep.glad.include()
	dep.freetype.include()
	dep.freetypeGl.include()

	dep.bgfx.compat()

	filter {}
end

function m.Graphics.link()
	m.Graphics.include()

	links { "Graphics" }

	m.Foundation.link()
	dep.glad.link()
	dep.bgfx.link()
	dep.freetype.link()
	dep.freetypeGl.link()
end

function m.Graphics.project()
	project "Graphics"
	kind "StaticLib"

	m.Graphics.include()

	files {
		paths.SRC_ROOT .. "graphics/**.h",
		paths.SRC_ROOT .. "graphics/**.cpp",
		--paths.RESOURCES_ROOT .. "fonts/**.ttf"
	}

	excludes {
		--paths.SRC_ROOT .. "graphics/bgfx/**",
	}

	filter("files:**.ttf")
		buildmessage 'Compiling resource: %{file.relpath}'

		-- One or more commands to run (required)
		buildcommands {
			'luac -o "%{cfg.objdir}/%{file.basename}.out" "%{file.relpath}"'
		}

		buildoutputs { '%{cfg.objdir}/%{file.basename}.h' }

	filter{}

	dep.bgfx.compat()
	util.liveppCompat()
end



function m.Ui.include()
	dependson { "configure" }

	m.Graphics.include()
	dep.yoga.include()
	dep.bgfx.compat()

	filter {}
end

function m.Ui.link()
	m.Ui.include()

	links { "Ui" }

	m.Graphics.link()
	dep.yoga.link()
end

function m.Ui.project()
	project "Ui"
	kind "StaticLib"

	m.Ui.include()

	files {
		paths.SRC_ROOT .. "ui/**.h",
		paths.SRC_ROOT .. "ui/**.cpp"
	}

	util.liveppCompat()
end



function m.Audio.include()
	dependson { "configure" }

	m.Foundation.include()
	dep.bgfx.compat()

	filter {}
end

function m.Audio.link()
	m.Audio.include()

	links { "Audio" }

	m.Foundation.link()
end

function m.Audio.project()
	project "Audio"
		kind "StaticLib"

		m.Audio.include()

		files {
			paths.SRC_ROOT .. "audio/**.h",
			paths.SRC_ROOT .. "audio/**.cpp"
		}

		util.liveppCompat()
end



function m.Application.include()
	dependson { "configure" }

	m.Graphics.include()
	m.Audio.include()
	dep.glfw.include()

	dep.bgfx.compat()

	filter {}
end

function m.Application.link()
	m.Application.include()

	links { "Application" }

	m.Graphics.link()
	m.Audio.link()
	dep.glfw.link()
end

function m.Application.project()
	project "Application"
		kind "StaticLib"

		m.Application.include()

		files {
			paths.SRC_ROOT .. "application/**.h",
			paths.SRC_ROOT .. "application/**.cpp"
		}

		util.liveppCompat()
end


function m.Engine.include()
	dependson { "configure" }

	m.Graphics.include()
	dep.box2d.include()

	dep.bgfx.compat()

	filter {}
end

function m.Engine.link()
	m.Engine.include()

	links { "engine" }

	m.Graphics.link()
	dep.box2d.link()
end

function m.Engine.project()
	project "Engine"
	kind "StaticLib"

	m.Engine.include()

	files {
		paths.SRC_ROOT .. "engine/**.h",
		paths.SRC_ROOT .. "engine/**.cpp"
	}

	util.liveppCompat()
end

local function tableContains(table, element)
	for _, value in pairs(table) do
		if value == element then
			return true
		end
	end

	return false
end

local function createStandalone(config, impl)
	project (config.name .. "-app")
		kind "ConsoleApp"

		impl()

		defines {
			"FW_USE_MINIAUDIO"
		}

		files {
			paths.SRC_ROOT .. "entry/main.cpp",
			paths.SRC_ROOT .. "entry/mainloop.cpp"
		}

		filter { "platforms:Emscripten" }
			buildoptions { "-gsource-map", "-matomics", "-mbulk-memory" }
			linkoptions { "-o %{string.lower(cfg.buildcfg)}/" .. config.name .. ".html", }

		filter { "platforms:Emscripten", "configurations:Debug*" }
			linkoptions { util.joinFlags(emscripten.flags.base, emscripten.flags.debug) }

		filter { "platforms:Emscripten", "configurations:Development*" }
			linkoptions { util.joinFlags(emscripten.flags.base, emscripten.flags.development) }

		filter { "platforms:Emscripten", "configurations:Release*" }
			linkoptions { util.joinFlags(emscripten.flags.base, emscripten.flags.release) }
		filter {}
end

local function createLivePp(config, impl)
	project (config.name .. "-live++")
		kind "ConsoleApp"

		impl()

		defines {
			"FW_USE_MINIAUDIO"
		}

		files {
			paths.SRC_ROOT .. "entry/mainlivepp.cpp",
			paths.SRC_ROOT .. "entry/mainloop.cpp"
		}

		util.liveppCompatLink()
end

function m.Application.create(config, impl)
	local crc32 = dofile(paths.SCRIPT_ROOT .. "dep/crc32.lua")

	if config.header == nil then
		config.header = config.name .. "Application.h"
	end

	if config.plugin ~= nil then
		if config.plugin.uniqueId == nil then
			config.plugin.uniqueId = string.format("%x", crc32(config.name)):sub(1, 4)
		end
	end

	-- Create project files if they don't exist
	local fullHeaderPath = paths.SRC_ROOT .. config.header
	if os.isfile(fullHeaderPath) == false then
		print("Creating project: " .. config.name)
		local header = io.readfile(paths.SRC_ROOT .. "templates/ApplicationTemplate.h")
		local source = io.readfile(paths.SRC_ROOT .. "templates/ApplicationTemplate.cpp")
		io.writefile(fullHeaderPath, util.interp(header, { name = config.name }))
		io.writefile(string.gsub(fullHeaderPath, ".h", ".cpp"), util.interp(source, { name = config.name }))
	end

	local function wrappedImpl()
		m.Application.link()
		m.Ui.link()

		defines {
			"APPLICATION_HEADER=" .. config.header,
			"APPLICATION_IMPL=" .. config.name .. "Application",
		}

		files {
			paths.SRC_ROOT .. "entry/ApplicationFactory.*"
		}

		impl()
	end

	if tableContains(config.targets, "standalone") then createStandalone(config, wrappedImpl) end
	if tableContains(config.targets, "standalone-livepp") then createLivePp(config, wrappedImpl) end

	if tableContains(config.targets, "standalone-iplug") then iplug2.createApp(config); wrappedImpl() end
	if tableContains(config.targets, "vst2") then iplug2.createVst2(config); wrappedImpl() end
	if tableContains(config.targets, "vst3") then iplug2.createVst3(config); wrappedImpl() end
	--if tableContains(config.targets, "aax") then iplug2.createAax(config); wrappedImpl() end
	--if tableContains(config.targets, "au") then iplug2.createAu(config); wrappedImpl() end
end

function m.ExampleApplication.project(name)
	m.Application.create({
		version = "0.0.1",
		name = name,
		header = "examples/" .. name .. ".h",
		author = "tommitytom",
		url = "https://tommitytom.co.uk",
		email = "fw@tommitytom.co.uk",
		copyright = "Tom Yaxley",
		targets = {
			"vst2",
			"vst3",
			"standalone",
			"standalone-livepp",
			"standalone-iplug",
			"au",
			"aax",
			"web"
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
			width = 1024,
			height = 768,
			fps = 60,
			vsync = true
		},

		plugin = {
			authorId = "tmtt",
			type = "synth",
			sharedResources = false,
		}
	}, function()
		m.Engine.link()
		dep.simdjson.link()

		includedirs {
			paths.SRC_ROOT .. "examples"
		}

		files {
			paths.SRC_ROOT .. "examples/" .. name .. ".*",
		}

		filter { "action:vs*" }
			buildoptions { "/bigobj" }
			files { paths.DEP_ROOT .. "entt/natvis/entt/*.natvis" }
	end)
end



--[[function m.ShaderReload.project()
	project "ShaderReload"
	kind "ConsoleApp"

	m.Graphics.link()
	m.Engine.link()
	m.Application.link()
	dep.simplefilewatcher.link()

	includedirs {
		"thirdparty",
		"thirdparty/spdlog/include",
		"thirdparty/sol",
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

	files {
		"src/shaderreload/**.h",
		"src/shaderreload/**.cpp"
	}
end]]


function m.Tests.project()
	project "Tests"
	kind "ConsoleApp"

	m.Application.link()
	m.Engine.link()

	files {
		paths.SRC_ROOT .. "tests/**.cpp"
	}
end

return m
