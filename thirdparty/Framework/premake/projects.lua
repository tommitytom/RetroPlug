local paths = dofile("paths.lua")
local dep = dofile(paths.SCRIPT_ROOT .. "dep/index.lua")
local iplug2 = dofile(paths.SCRIPT_ROOT .. "dep/iplug2.lua")
local util = dofile(paths.SCRIPT_ROOT .. "util.lua")

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

	sysincludedirs {
		paths.DEP_ROOT,
		paths.DEP_ROOT .. "spdlog/include"
	}

	includedirs {
		paths.SRC_ROOT,
		paths.GENERATED_ROOT,
		paths.RESOURCES_ROOT
	}

	dep.lua.include()
	dep.bgfx.compat()

	filter {}
end

function m.Foundation.link()
	m.Foundation.include()

	links { "Foundation" }

	dep.lua.link()
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

	util.liveppCompat()
end


function m.Graphics.include()
	dependson { "configure" }

	m.Foundation.include()
	dep.bgfx.include()
	dep.glfw.include()
	dep.freetype.include()

	dep.bgfx.compat()

	filter {}
end

function m.Graphics.link()
	m.Graphics.include()

	links { "Graphics" }

	m.Foundation.link()
	dep.bgfx.link()
	dep.freetype.link()
end

function m.Graphics.project()
	project "Graphics"
	kind "StaticLib"

	m.Graphics.include()

	files {
		paths.SRC_ROOT .. "graphics/**.h",
		paths.SRC_ROOT .. "graphics/**.cpp"
	}

	util.liveppCompat()
end



function m.Ui.include()
	dependson { "configure" }

	m.Graphics.include()
	dep.bgfx.compat()

	filter {}
end

function m.Ui.link()
	m.Ui.include()

	links { "Ui" }

	m.Graphics.link()
end

function m.Ui.project()
	project "Ui"
	kind "StaticLib"

	m.Graphics.include()

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
	project (name)
		kind "ConsoleApp"

		m.Application.link()
		m.Engine.link()
		m.Ui.link()

		defines {
			"EXAMPLE_IMPL=" .. name
		}

		includedirs {
			paths.SRC_ROOT .. "examples"
		}

		files {
			paths.SRC_ROOT .. "examples/" .. name .. ".*",
			paths.SRC_ROOT .. "examples/main.cpp"
		}

		filter { "action:vs*" }
			files { paths.DEP_ROOT .. "entt/natvis/entt/*.natvis" }

		--[[filter { "options:emscripten" }
			buildoptions { "-matomics", "-mbulk-memory" }

		filter { "options:emscripten", "configurations:Debug" }
			linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEBUG_FLAGS) }

		filter { "options:emscripten", "configurations:Release" }
			linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_RELEASE_FLAGS) }]]

	project (name .. "-live++")
		kind "ConsoleApp"

		m.Application.link()
		m.Engine.link()
		m.Ui.link()

		defines {
			"EXAMPLE_IMPL=" .. name
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
			files { paths.DEP_ROOT .. "entt/natvis/entt/*.natvis" }

		util.liveppCompat()

	iplug2.createApp(name)
		m.Application.link()
		m.Engine.link()
		m.Ui.link()

		defines {
			"EXAMPLE_IMPL=" .. name
		}

		includedirs {
			paths.SRC_ROOT .. "examples",
			paths.SRC_ROOT .. "plugin"
		}

		files {
			paths.SRC_ROOT .. "plugin/*",
			paths.SRC_ROOT .. "examples/" .. name .. ".*"
		}

		filter { "action:vs*" }
			files { paths.DEP_ROOT .. "entt/natvis/entt/*.natvis" }

		filter {}
end



--[[function m.ShaderReload.project()
	project "ShaderReload"
	kind "ConsoleApp"

	m.Graphics.link()
	m.Engine.link()
	m.Application.link()
	dep.simplefilewatcher.link()

	sysincludedirs {
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