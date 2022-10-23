local dep = dofile("dep/index.lua")
--local util = dofile("util.lua")
local util = dofile("thirdparty/Framework/premake/util.lua")

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
		_ROOT_PATH .. "thirdparty",
		_ROOT_PATH .. "thirdparty/spdlog/include"
	}

	includedirs {
		_ROOT_PATH .. "src",
		_ROOT_PATH .. "generated",
		_ROOT_PATH .. "resources"
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
		_ROOT_PATH .. "src/foundation/**.h",
		_ROOT_PATH .. "src/foundation/**.cpp"
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
		_ROOT_PATH .. "thirdparty",
		_ROOT_PATH .. "thirdparty/spdlog/include"
	}

	includedirs {
		_ROOT_PATH .. "src",
		_ROOT_PATH .. "generated",
		_ROOT_PATH .. "resources"
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
		_ROOT_PATH .. "src/graphics/**.h",
		_ROOT_PATH .. "src/graphics/**.cpp"
	}

	util.liveppCompat()
end



function m.Ui.include()
	dependson { "configure" }

	m.Graphics.include()

	sysincludedirs {
		_ROOT_PATH .. "thirdparty",
		_ROOT_PATH .. "thirdparty/spdlog/include"
	}

	includedirs {
		_ROOT_PATH .. "src",
		_ROOT_PATH .. "generated",
		_ROOT_PATH .. "resources"
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
		_ROOT_PATH .. "src/ui/**.h",
		_ROOT_PATH .. "src/ui/**.cpp"
	}

	util.liveppCompat()
end



function m.Audio.include()
	dependson { "configure" }

	m.Foundation.include()

	sysincludedirs {
		_ROOT_PATH .. "thirdparty",
		_ROOT_PATH .. "thirdparty/spdlog/include"
	}

	includedirs {
		_ROOT_PATH .. "src",
		_ROOT_PATH .. "generated",
		_ROOT_PATH .. "resources"
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
			_ROOT_PATH .. "src/audio/**.h",
			_ROOT_PATH .. "src/audio/**.cpp"
		}

		util.liveppCompat()
end



function m.Application.include()
	dependson { "configure" }

	m.Graphics.include()
	m.Audio.include()
	dep.glfw.include()

	sysincludedirs {
		_ROOT_PATH .. "thirdparty",
		_ROOT_PATH .. "thirdparty/spdlog/include"
	}

	includedirs {
		_ROOT_PATH .. "src",
		_ROOT_PATH .. "generated",
		_ROOT_PATH .. "resources"
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
			_ROOT_PATH .. "src/application/**.h",
			_ROOT_PATH .. "src/application/**.cpp"
		}

		util.liveppCompat()
end


function m.Engine.include()
	dependson { "configure" }

	m.Graphics.include()
	dep.box2d.include()

	sysincludedirs {
		_ROOT_PATH .. "thirdparty",
		_ROOT_PATH .. "thirdparty/spdlog/include"
	}

	includedirs {
		_ROOT_PATH .. "src",
		_ROOT_PATH .. "generated",
		_ROOT_PATH .. "resources"
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
		_ROOT_PATH .. "src/engine/**.h",
		_ROOT_PATH .. "src/engine/**.cpp"
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
			_ROOT_PATH .. "src/examples"
		}

		files {
			_ROOT_PATH .. "src/examples/" .. name .. ".*",
			_ROOT_PATH .. "src/examples/main.cpp"
		}

		filter { "action:vs*" }
			files { _ROOT_PATH .. "thirdparty/entt/natvis/entt/*.natvis" }

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
			_ROOT_PATH .. "src/examples"
		}

		files {
			_ROOT_PATH .. "src/examples/" .. name .. ".*",
			_ROOT_PATH .. "src/examples/mainlivepp.cpp",
			_ROOT_PATH .. "src/examples/mainloop.cpp"
		}

		filter { "action:vs*" }
			files { _ROOT_PATH .. "thirdparty/entt/natvis/entt/*.natvis" }

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
		_ROOT_PATH .. "src/tests/**.cpp"
	}
end

return m
