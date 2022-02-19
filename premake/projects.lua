local dep = dofile("dep/index.lua")
local util = dofile("util.lua")

local EMSDK_FLAGS = {
	"-s WASM=1",
	--"-s LLD_REPORT_UNDEFINED",
	--[[-s EXPORTED_FUNCTIONS='["_main", "_retroplug_get"]']]
	[[-s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]']],
	--"-s EXPORT_ES6=1",
	--"-s MODULARIZE=1",
	--"-s EXPORT_NAME=RetroPlugModule",
	--"-s SINGLE_FILE=1",
	--"-s TOTAL_MEMORY=512MB",
	"-s ENVIRONMENT=web,worker,audioworklet",
	"-s ALLOW_MEMORY_GROWTH=1",
	--"-s USE_ES6_IMPORT_META=0",
	--"--bind"
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
	"-Oz",
	--"-closure",
	"-o release/index.html"
}

local m = {
	RetroPlug = {},
	Application = {},
	ExampleApplication = {},
	OffsetCalculator = {},
	Plugin = {}
}

function m.RetroPlug.include()
	dependson { "configure" }

	dep.bgfx.include()
	dep.glfw.include()
	dep.SameBoy.include()
	dep.liblsdj.include()
	dep.lua.include()

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
end

function m.RetroPlug.link()
	m.RetroPlug.include()

	dep.SameBoy.link()
	dep.glfw.link()
	dep.bgfx.link()
	dep.liblsdj.link()
	dep.lua.link()
	dep.r8brain.link()

	links { "RetroPlug" }
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
		--"src/generated/*.h",
		--"src/generated/*.cpp",
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

	filter { "options:not emscripten" }
		dependson { "ScriptCompiler" }

		prebuildcommands {
			"%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/ScriptCompiler ../../src/compiler.config.lua %{cfg.platform}"
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

function m.Application.project()
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

	--filter { "options:not emscripten" }
	--	excludes { "src/RetroPlugWrap.cpp" }

	filter { "options:emscripten" }
		buildoptions { "-matomics", "-mbulk-memory" }

	filter { "options:emscripten", "configurations:Debug" }
		--buildoptions { "--bind" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEBUG_FLAGS) }

	filter { "options:emscripten", "configurations:Release" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_RELEASE_FLAGS) }

	filter {}
end

function m.Application.projectLivepp()
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

return m
