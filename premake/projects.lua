local dep = dofile("dep/index.lua")
local util = dofile("thirdparty/Framework/premake/util.lua")
local fwProjects = dofile("thirdparty/Framework/premake/projects.lua")
local fwDeps = dofile("thirdparty/Framework/premake/dep/index.lua")
local iplug2 = dofile("thirdparty/Framework/premake/dep/iplug2.lua")

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
	Core = {},
	SameBoyPlug = {},
	RetroPlug = {},
	Application = {},
	OffsetCalculator = {},
	Plugin = {},
	Tests = {}
}



function m.Core.include()
	dependson { "configure" }

	fwProjects.Foundation.include()
	fwProjects.Graphics.include()
	fwProjects.Ui.include()
	fwProjects.Audio.include()
	fwProjects.Application.include()
	fwDeps.lua.include()
	dep.minizip.include()
	dep.SameBoy.include()

	sysincludedirs {
		"thirdparty",
		"thirdparty/Framework/src",
		"thirdparty/spdlog/include",
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

	fwDeps.bgfx.compat()

	filter {}
end

function m.Core.link()
	m.Core.include()

	links { "Core" }

	fwProjects.Foundation.link()
	fwProjects.Graphics.link()
	fwProjects.Ui.link()
	fwProjects.Audio.link()
	fwProjects.Application.link()
	fwDeps.lua.link()
	dep.minizip.link()
	dep.SameBoy.link()
end

function m.Core.project()
	project "Core"
	kind "StaticLib"

	m.Core.include()

	files {
		"src/core/**.h",
		"src/core/**.cpp",
		"src/generated/*.h",
		"src/generated/*_%{cfg.platform}.cpp",
	}

	util.liveppCompat()
end

function m.SameBoyPlug.include()
	dependson { "configure" }

	dep.SameBoy.include()
	m.Core.include()

	sysincludedirs {
		"thirdparty",
		"thirdparty/Framework/src",
		"thirdparty/spdlog/include",
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

	filter { "toolset:clang" }
		disablewarnings { "missing-braces", "c99-designator" }

	filter {}

	fwDeps.bgfx.compat()

	filter {}
end

function m.SameBoyPlug.link()
	m.SameBoyPlug.include()

	links { "SameBoyPlug" }

	m.Core.link()
	dep.SameBoy.link()
end

function m.SameBoyPlug.project()
	project "SameBoyPlug"
	kind "StaticLib"

	m.SameBoyPlug.include()

	filter { "system:windows" }
		toolset "clang"

	files {
		"src/sameboy/**.h",
		"src/sameboy/**.hpp",
		"src/sameboy/**.cpp",
		"src/sameboy/**.c",
	}

	--util.liveppCompat()
end

function m.RetroPlug.include()
	dependson { "configure" }

	m.Core.include()
	m.SameBoyPlug.include()
	dep.liblsdj.include()
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

	fwDeps.bgfx.compat()

	filter {}
end

function m.RetroPlug.link()
	m.RetroPlug.include()

	links { "RetroPlug" }

	m.SameBoyPlug.link()
	fwDeps.bgfx.link()
	fwDeps.glfw.link()
	dep.liblsdj.link()
	fwDeps.lua.link()
	dep.r8brain.link()
	dep.minizip.link()
end

function m.RetroPlug.project()
	project "RetroPlug"
	kind "StaticLib"

	m.RetroPlug.include()

	files {
		"src/*.h",
		"src/app/RetroPlugApplication.h",
		"src/lsdj/**.h",
		"src/lsdj/**.cpp",
		"src/node/**.h",
		"src/node/**.cpp",
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

function m.Application.project()
	project "RetroPlugApp"
	kind "ConsoleApp"

	defines {
		"APPLICATION_IMPL=RetroPlugApplication"
	}

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

function m.Application.iplugProject()
	iplug2.createApp("config.lua")

	m.RetroPlug.link()

	defines {
		"APPLICATION_IMPL=RetroPlugApplication"
	}

	--[[files {
		"src/app/**.h",
		"src/app/**.cpp"
	}]]
	excludes {
		--"src/app/main.cpp",
		"src/app/OffsetCalculatorMain.cpp"
	}

	util.liveppCompat()
end

function m.Application.iplugVst2()
	iplug2.createVst2("config.lua")

	m.RetroPlug.link()

	defines {
		"APPLICATION_IMPL=RetroPlugApplication"
	}

	--[[files {
		"src/app/**.h",
		"src/app/**.cpp"
	}]]
	excludes {
		--"src/app/main.cpp",
		"src/app/OffsetCalculatorMain.cpp"
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