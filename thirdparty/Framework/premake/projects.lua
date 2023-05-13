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
	Application = {},
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


function m.ExampleApplication.project(name)
	local crc32 = dofile(paths.SCRIPT_ROOT .. "dep/crc32.lua")

	local config = {
		version = "0.0.1",
		name = name,
		author = "tommitytom",
		url = "https://tommitytom.co.uk",
		email = "fw@tommitytom.co.uk",
		copyright = "Tom Yaxley",

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
			uniqueId = string.format("%x", crc32(name)):sub(1, 4),
			authorId = "tmtt",
			type = "synth",
			sharedResources = false,
		}
	}

	-- Create project files if they don't exist
	local exampleRoot = paths.SRC_ROOT .. "examples/"
	if os.isfile(exampleRoot .. name .. ".h") == false then
		print("Creating example: " .. name)
		local header = io.readfile(exampleRoot .. "ExampleTemplate.h")
		local source = io.readfile(exampleRoot .. "ExampleTemplate.cpp")
		io.writefile(exampleRoot .. name .. ".h", util.interp(header, { name = name }))
		io.writefile(exampleRoot .. name .. ".cpp", util.interp(source, { name = name }))
	end

	project (name)
		kind "ConsoleApp"

		m.Application.link()
		m.Engine.link()
		m.Ui.link()
		dep.simdjson.link()

		defines {
			"APPLICATION_HEADER=" .. name,
			"APPLICATION_IMPL=" .. name .. "Application",
			"FW_USE_MINIAUDIO"
		}

		includedirs {
			paths.SRC_ROOT .. "examples"
		}

		files {
			paths.SRC_ROOT .. "examples/" .. name .. ".*",
			paths.SRC_ROOT .. "examples/main.cpp"
		}

		filter { "action:vs*" }
			buildoptions { "/bigobj" }
			files { paths.DEP_ROOT .. "entt/natvis/entt/*.natvis" }

		filter { "platforms:Emscripten" }
			buildoptions { "-gsource-map", "-matomics", "-mbulk-memory" }
			linkoptions { "-o %{string.lower(cfg.buildcfg)}/" .. name .. ".html", }

		filter { "platforms:Emscripten", "configurations:Debug*" }
			linkoptions { util.joinFlags(emscripten.flags.base, emscripten.flags.debug) }

		filter { "platforms:Emscripten", "configurations:Development*" }
			linkoptions { util.joinFlags(emscripten.flags.base, emscripten.flags.development) }

		filter { "platforms:Emscripten", "configurations:Release*" }
			linkoptions { util.joinFlags(emscripten.flags.base, emscripten.flags.release) }
		filter {}

	project (name .. "-live++")
		kind "ConsoleApp"

		m.Application.link()
		m.Engine.link()
		m.Ui.link()
		dep.simplefilewatcher.link()
		dep.simdjson.link()

		defines {
			"APPLICATION_HEADER=" .. name,
			"APPLICATION_IMPL=" .. name .. "Application",
			"FW_USE_MINIAUDIO"
		}

		includedirs {
			paths.SRC_ROOT .. "examples"
		}

		files {
			paths.SRC_ROOT .. "examples/" .. name .. ".*",
			paths.SRC_ROOT .. "examples/mainlivepp.cpp",
			paths.SRC_ROOT .. "examples/mainloop.cpp"
		}

		filter { "action:vs*" }
			buildoptions { "/bigobj" }
			files { paths.DEP_ROOT .. "entt/natvis/entt/*.natvis" }

		util.liveppCompat()
--[[
	iplug2.createApp(config)
		m.Application.link()
		m.Engine.link()
		m.Ui.link()

		defines {
			"APPLICATION_HEADER=" .. name,
			"APPLICATION_IMPL=" .. name .. "Application",
		}

		includedirs {
			paths.SRC_ROOT .. "examples"
		}

		files {
			paths.SRC_ROOT .. "examples/" .. name .. ".*"
		}

		filter { "action:vs*" }
			buildoptions { "/bigobj" }
			files { paths.DEP_ROOT .. "entt/natvis/entt/*.natvis" }

		filter {}

	iplug2.createVst2(config)
		m.Application.link()
		m.Engine.link()
		m.Ui.link()

		defines {
			"APPLICATION_HEADER=" .. name,
			"APPLICATION_IMPL=" .. name .. "Application",
		}

		includedirs {
			paths.SRC_ROOT .. "examples",
		}

		files {
			paths.SRC_ROOT .. "examples/" .. name .. ".*"
		}

		filter { "action:vs*" }
			buildoptions { "/bigobj" }
			files { paths.DEP_ROOT .. "entt/natvis/entt/*.natvis" }

		filter {}

	iplug2.createVst3(config)
		m.Application.link()
		m.Engine.link()
		m.Ui.link()

		defines {
			"APPLICATION_IMPL=" .. name
		}

		includedirs {
			paths.SRC_ROOT .. "examples",
		}

		files {
			paths.SRC_ROOT .. "examples/" .. name .. ".*"
		}

		filter { "action:vs*" }
			buildoptions { "/bigobj" }
			files { paths.DEP_ROOT .. "entt/natvis/entt/*.natvis" }

		filter {}]]
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
