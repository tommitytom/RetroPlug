local dep = dofile("dep/index.lua")
local util = dofile("util.lua")

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
		"thirdparty",
		"thirdparty/spdlog/include"
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

	dep.bgfx.compat()

	filter {}
end

function m.Foundation.link()
	m.Foundation.include()

	links { "Foundation" }
end

function m.Foundation.project()
	project "Foundation"
	kind "StaticLib"

	m.Foundation.include()

	files {
		"src/foundation/**.h",
		"src/foundation/**.cpp"
	}

	util.liveppCompat()
end


function m.Graphics.include()
	dependson { "configure" }

	m.Foundation.include()
	dep.bgfx.include()
	dep.glfw.include()
	dep.freetype.include()

	sysincludedirs {
		"thirdparty",
		"thirdparty/spdlog/include"
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

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
		"src/graphics/**.h",
		"src/graphics/**.cpp"
	}

	util.liveppCompat()
end



function m.Ui.include()
	dependson { "configure" }

	m.Graphics.include()

	sysincludedirs {
		"thirdparty",
		"thirdparty/spdlog/include"
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

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
		"src/ui/**.h",
		"src/ui/**.cpp"
	}

	util.liveppCompat()
end



function m.Audio.include()
	dependson { "configure" }

	m.Foundation.include()

	sysincludedirs {
		"thirdparty",
		"thirdparty/spdlog/include"
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

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
			"src/audio/**.h",
			"src/audio/**.cpp"
		}

		util.liveppCompat()
end



function m.Application.include()
	dependson { "configure" }

	m.Graphics.include()
	m.Audio.include()
	dep.glfw.include()

	sysincludedirs {
		"thirdparty",
		"thirdparty/spdlog/include"
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

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
			"src/application/**.h",
			"src/application/**.cpp"
		}

		util.liveppCompat()
end


function m.Engine.include()
	dependson { "configure" }

	m.Graphics.include()
	dep.box2d.include()

	sysincludedirs {
		"thirdparty",
		"thirdparty/spdlog/include"
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

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
		"src/engine/**.h",
		"src/engine/**.cpp"
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
			"src/examples"
		}

		files {
			"src/examples/" .. name .. ".*",
			"src/examples/main.cpp"
		}

		filter { "action:vs*" }
			files { "thirdparty/entt/natvis/entt/*.natvis" }

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
			"src/examples"
		}

		files {
			"src/examples/" .. name .. ".*",
			"src/examples/mainlivepp.cpp",
			"src/examples/mainloop.cpp"
		}

		filter { "action:vs*" }
			files { "thirdparty/entt/natvis/entt/*.natvis" }

		util.liveppCompat()
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
		"src/tests/**.cpp"
	}
end

return m
