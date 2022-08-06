local dep = dofile("dep/index.lua")
local util = dofile("util.lua")

local EMSDK_FLAGS = {
	"-s WASM=1",
	--"-s LLD_REPORT_UNDEFINED",
	[[-s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]']],
	--"-s TOTAL_MEMORY=512MB",
	"-s ENVIRONMENT=web,worker,audioworklet",
	"-s ALLOW_MEMORY_GROWTH=1",
	--"-s USE_ES6_IMPORT_META=0",
	"-s USE_PTHREADS=1",
	--"-s PTHREAD_POOL_SIZE=2",
	"-s USE_GLFW=3",
	"-s USE_WEBGL2=1",
	"-s FORCE_FILESYSTEM=1",
	--"-s FULL_ES3=1",
	--"-s MIN_WEBGL_VERSION=2",
	--"-s MAX_WEBGL_VERSION=2", -- https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html#opengl-support-webgl-subset
	"-s NO_DISABLE_EXCEPTION_CATCHING=1",
	"-s ASYNCIFY",

	"-lidbfs.js",

	"--shell-file ../../templates/shell_minimal.html",
	"--post-js ../../templates/processor.js",

	"-fexceptions",
}

local EMSDK_DEBUG_FLAGS = {
	"-s ASSERTIONS=1",
	"-g",
	"-o debug/index.html"
}

local EMSDK_RELEASE_FLAGS = {
	"-s ASSERTIONS=1",
	--"-s ELIMINATE_DUPLICATE_FUNCTIONS=1",
	--"-s MINIMAL_RUNTIME",
	"-g",
	"-O3",
	"-closure",
	"-o release/index.html"
}

local m = {
	Foundation = {},
	Graphics = {},
	Application = {},
	RetroPlug = {},
	RetroPlugApp = {},
	ExampleApplication = {},
	OffsetCalculator = {},
	Plugin = {},
	Tests = {},
	BgfxBasic = {},
	Solitaire = {},
	PhysicsTest = {},
	Engine = {}
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


function m.Application.include()
	dependson { "configure" }

	m.Graphics.include()
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


function m.RetroPlug.include()
	dependson { "configure" }

	m.Foundation.include()
	m.Graphics.include()
	m.Application.include()
	dep.SameBoy.include()
	dep.liblsdj.include()
	dep.lua.include()
	dep.minizip.include()

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

	dep.bgfx.compat()

	filter {}
end

function m.RetroPlug.link()
	m.RetroPlug.include()

	links { "RetroPlug" }

	m.Foundation.link()
	m.Graphics.link()
	m.Application.link()
	dep.SameBoy.link()
	dep.bgfx.link()
	dep.glfw.link()
	dep.liblsdj.link()
	dep.lua.link()
	dep.r8brain.link()
	dep.minizip.link()
	dep.freetype.link()
end

function m.RetroPlug.project()
	project "RetroPlug"
	kind "StaticLib"

	m.RetroPlug.include()

	files {
		"src/*.h",
		"src/RetroPlug.cpp",
		"src/core/**.h",
		"src/core/**.cpp",
		"src/generated/lua/*_%{cfg.platform}.h",
		"src/generated/lua/*_%{cfg.platform}.cpp",
		"src/lsdj/**.h",
		"src/lsdj/**.cpp",
		"src/node/**.h",
		"src/node/**.cpp",
		"src/platform/**.h",
		"src/platform/**.cpp",
		"src/util/**.h",
		"src/util/**.cpp",
		"src/ui/**.h",
		"src/ui/**.cpp",
	}

	util.liveppCompat()
end

function m.Plugin.include()
	m.RetroPlug.include()

	includedirs {
		"resource"
	}
end

function m.Plugin.project()
	m.RetroPlug.link()

	files {
		"src/plugin/**.h",
		"src/plugin/**.cpp"
	}
end

function m.RetroPlugApp.project()
	project "RetroPlugApp"
	kind "ConsoleApp"

	m.RetroPlug.link()

	files {
		"src/app/**.h",
		"src/app/**.cpp"
	}
	excludes {
		"src/app/mainloop.cpp",
		"src/app/mainlivepp.cpp",
		"src/app/OffsetCalculatorMain.cpp"
	}

	filter { "system:linux" }
		linkoptions { "-no-pie" } -- maybe put in premake.lua?

	filter { "options:emscripten", "configurations:Debug" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEBUG_FLAGS) }

	filter { "options:emscripten", "configurations:Release" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_RELEASE_FLAGS) }

	filter {}
end

function m.RetroPlugApp.projectLivepp()
	project "RetroPlugApp-live++"
	kind "ConsoleApp"

	m.RetroPlug.link()

	files {
		"src/app/**.h",
		"src/app/**.cpp"
	}
	excludes {
		"src/app/main.cpp",
		"src/app/OffsetCalculatorMain.cpp"
	}

	util.liveppCompat()
end


function m.ExampleApplication.project()
	project "ExampleApplication"
	kind "ConsoleApp"

	m.RetroPlug.link()
	dep.box2d.link()

	files {
		"src/uiexamples/**.h",
		"src/uiexamples/**.cpp"
	}
	excludes {
		"src/uiexamples/mainloop.cpp",
		"src/uiexamples/mainlivepp.cpp"
	}

	filter { "options:emscripten" }
		buildoptions { "-matomics", "-mbulk-memory" }

	filter { "options:emscripten", "configurations:Debug" }
		--buildoptions { "--bind" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEBUG_FLAGS) }

	filter { "options:emscripten", "configurations:Release" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_RELEASE_FLAGS) }
end

function m.ExampleApplication.projectLivepp()
	project "ExampleApplication-live++"
	kind "ConsoleApp"

	m.RetroPlug.link()

	files {
		"src/uiexamples/**.h",
		"src/uiexamples/**.cpp"
	}
	excludes {
		"src/uiexamples/main.cpp",
	}

	util.liveppCompat()
end

function m.BgfxBasic.project()
	project "BgfxBasic"
	kind "ConsoleApp"

	m.Graphics.link()
	m.Application.link()
	dep.box2d.link()

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
		"src/bgfxbasic/**.h",
		"src/bgfxbasic/**.cpp"
	}

	filter { "options:emscripten" }
		buildoptions { "-matomics", "-mbulk-memory" }

	filter { "options:emscripten", "configurations:Debug" }
		--buildoptions { "--bind" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEBUG_FLAGS) }

	filter { "options:emscripten", "configurations:Release" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_RELEASE_FLAGS) }
end

function m.Solitaire.project()
	project "Solitaire"
	kind "ConsoleApp"

	m.Graphics.link()
	m.Engine.link()
	m.Application.link()

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
		"src/solitaire/**.h",
		"src/solitaire/**.cpp"
	}

	filter { "options:emscripten" }
		buildoptions { "-matomics", "-mbulk-memory" }

	filter { "options:emscripten", "configurations:Debug" }
		--buildoptions { "--bind" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEBUG_FLAGS) }

	filter { "options:emscripten", "configurations:Release" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_RELEASE_FLAGS) }
end

function m.PhysicsTest.project()
	project "PhysicsTest"
	kind "ConsoleApp"

	m.Graphics.link()
	m.Application.link()
	dep.box2d.link()

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
		"src/physicstest/**.h",
		"src/physicstest/**.cpp"
	}

	filter { "options:emscripten" }
		buildoptions { "-matomics", "-mbulk-memory" }

	filter { "options:emscripten", "configurations:Debug" }
		--buildoptions { "--bind" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEBUG_FLAGS) }

	filter { "options:emscripten", "configurations:Release" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_RELEASE_FLAGS) }
end

function m.Solitaire.projectLivepp()
	project "Solitaire-live++"
	kind "ConsoleApp"

	m.Graphics.link()
	m.Application.link()
	m.Engine.link()

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
		"src/solitaire/**.h",
		"src/solitaire/**.cpp"
	}

	util.liveppCompat()
end

function m.OffsetCalculator.project()
	project "LsdjOffsetCalculator"
	kind "ConsoleApp"

	m.RetroPlug.link()

	files {
		"src/app/OffsetCalculatorMain.cpp"
	}
	excludes {
		"src/app/main.cpp",
		"src/app/mainloop.cpp",
		"src/app/mainlivepp.cpp",
	}

	util.liveppCompat()
end

function m.Tests.project()
	project "Tests"
	kind "ConsoleApp"

	m.RetroPlug.link()

	files {
		"src/tests/**.cpp"
	}
end

return m
