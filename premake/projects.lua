local dep = dofile("dep/index.lua")
local util = dofile("thirdparty/orb/premake/util.lua")
local fwProjects = dofile("thirdparty/orb/premake/projects.lua")
local fwDeps = dofile("thirdparty/orb/premake/dep/index.lua")
local iplug2 = dofile("thirdparty/orb/premake/dep/iplug2.lua")

local EMSDK_FLAGS = {
	--"-s TOTAL_MEMORY=512MB",

	"-s STACK_SIZE=7MB",
	"-s DEFAULT_PTHREAD_STACK_SIZE=5MB",

	"-s ENVIRONMENT=web,worker",
	"-s ALLOW_MEMORY_GROWTH=1",
	"-s INITIAL_MEMORY=512MB",
	"-s USE_PTHREADS=1",
	"-s PTHREAD_POOL_SIZE=8",
	"-s AUDIO_WORKLET=1",
	"-s WASM_WORKERS=1",

	--"-s EXPORT_NAME=RetroPlugModule",

	"-s USE_ZLIB",

	"-s NO_EXIT_RUNTIME=1",

	"-lembind",
	"--emit-tsd RetroPlug.d.ts",
	"--no-entry",

	"-s WASM_BIGINT",
}

-- if toggling this, remember to update exceptions in util.lua. this line:
-- buildoptions { "-fwasm-exceptions" }
local useWasmFs = true;
if useWasmFs then
	table.insert(EMSDK_FLAGS, "-s WASMFS=1")
	table.insert(EMSDK_FLAGS, "-s JSPI=1")
	table.insert(EMSDK_FLAGS, "-fwasm-exceptions")
else
	table.insert(EMSDK_FLAGS, "-lidbfs.js")
	table.insert(EMSDK_FLAGS, "-s FORCE_FILESYSTEM=1")
	table.insert(EMSDK_FLAGS, "-s ASYNCIFY=1")
	table.insert(EMSDK_FLAGS, "-fexceptions")
end

local EMSDK_DEBUG_FLAGS = {
	"-s ASSERTIONS=1",
	"-g",
	"-O0",
	--"-gsource-map",
	"-s SAFE_HEAP=2",
	"-s STACK_OVERFLOW_CHECK=1",
	"-s WARN_UNALIGNED=1",
	--"-s ERROR_ON_WASM_CHANGES_AFTER_LINK", -- Makes sure no JS post-processing happens after linking, to keep iteration time quick
	"-s WEBAUDIO_DEBUG=1"
}

local EMSDK_DEVELOPMENT_FLAGS = {
	"-s ASSERTIONS=1",
	"-g",
	"-O1",
	"-s SAFE_HEAP=1",
	--"-s STACK_OVERFLOW_CHECK=1",
	--"-s WARN_UNALIGNED=1",
	--"-s ERROR_ON_WASM_CHANGES_AFTER_LINK", -- Makes sure no JS post-processing happens after linking, to keep iteration time quick
}

local EMSDK_RELEASE_FLAGS = {
	--"-s ASSERTIONS=1",
	"-s ELIMINATE_DUPLICATE_FUNCTIONS=1",
	--"-s ERROR_ON_WASM_CHANGES_AFTER_LINK", -- Makes sure no JS post-processing happens after linking, to keep iteration time quick
	--"-s MINIMAL_RUNTIME",
	"-g",
	"-gseparate-dwarf",
	"-O3",
	"-closure",
	--"-s SAFE_HEAP=2",
	--"-s STACK_OVERFLOW_CHECK=1",
	--"-s WARN_UNALIGNED=1",
	--"-s WEBAUDIO_DEBUG=1"
}

local m = {
	Core = {},
	SameBoyPlug = {},
	MesenPlug = {},
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
	--dep.minizip.include()
	dep.SameBoy.include()
	dep.reflcpp.include()
	dep.enkits.include()

	includedirs {
		"thirdparty",
		"thirdparty/orb/src",
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

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
	--dep.minizip.link()
	dep.SameBoy.link()
	dep.reflcpp.link()
	dep.enkits.link()
end

function m.Core.project()
	project "Core"
	kind "StaticLib"

	m.Core.include()

	files {
		"src/core/**.h",
		"src/core/**.cpp",
		"src/retroplug-generated/*.h",
		"src/retroplug-generated/*_%{cfg.architecture}.cpp",
	}

	util.liveppCompat()
end

function m.SameBoyPlug.include()
	dependson { "configure" }

	dep.SameBoy.include()
	m.Core.include()

	includedirs {
		"thirdparty",
		"thirdparty/orb/src",
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}

	filter { "toolset:clang" }
		buildoptions {
			"-Wno-unused-variable",
			"-Wno-unused-function",
			"-Wno-missing-braces",
			"-Wno-switch",
			"-Wno-int-in-bool-context",
			"-Wno-c99-designator"
		}
		disablewarnings { "missing-braces", "c99-designator" }

	filter {}

	filter {}
end

function m.SameBoyPlug.link()
	m.SameBoyPlug.include()

	links { "SameBoyPlug" }

	m.Core.link()
	dep.SameBoy.link()
end

local SAMEBOY_DIR = "thirdparty/SameBoy"

function m.SameBoyPlug.project()
	project "SameBoyPlug"
	kind "StaticLib"

	m.SameBoyPlug.include()

	filter { "system:windows" }
		toolset "clang"
		includedirs { SAMEBOY_DIR .. "/Windows" }

	filter {}

	files {
		"src/sameboy/**.h",
		"src/sameboy/**.hpp",
		"src/sameboy/**.cpp",
		"src/sameboy/**.c",
	}

	--util.liveppCompat()
end

function m.MesenPlug.include()
	dependson { "configure" }

	dep.mesen.include()
	dep.serial.include()
	m.Core.include()
end

function m.MesenPlug.link()
	m.MesenPlug.include()

	links { "MesenPlug" }

	m.Core.link()
	dep.mesen.link()
	dep.serial.link()
end

function m.MesenPlug.project()
	project "MesenPlug"
	kind "StaticLib"

	m.MesenPlug.include()

	filter {}

	files {
		"src/mesen/**.h",
		"src/mesen/**.cpp",
	}

	filter {}
end

function m.RetroPlug.include()
	dependson { "configure" }

	m.Core.include()
	m.SameBoyPlug.include()
	m.MesenPlug.include()
	dep.liblsdj.include()
	--dep.minizip.include()

	includedirs {
		"thirdparty",
		"thirdparty/sol",
	}

	includedirs {
		"src",
		"src/ecs",
		"generated",
		"resources"
	}

	filter { "platforms:Emscripten" }
		disablewarnings { "character-conversion" }

	filter {}
end

function m.RetroPlug.link()
	m.RetroPlug.include()

	links { "RetroPlug" }

	m.SameBoyPlug.link()
	m.MesenPlug.link()
	fwDeps.glfw.link()
	dep.liblsdj.link()
	fwDeps.lua.link()
	dep.r8brain.link()
	dep.enkits.link()
	--dep.minizip.link()

	filter { "platforms:Emscripten" }
		disablewarnings { "character-conversion" }

	filter { "platforms:Emscripten", "configurations:Debug" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEBUG_FLAGS) }

	filter { "platforms:Emscripten", "configurations:Development" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_DEVELOPMENT_FLAGS) }

	filter { "platforms:Emscripten", "configurations:Release" }
		linkoptions { util.joinFlags(EMSDK_FLAGS, EMSDK_RELEASE_FLAGS) }

	filter {}
end

function m.RetroPlug.project()
	project "RetroPlug"
	kind "StaticLib"

	m.RetroPlug.include()

	files {
		"src/*.h",
		"src/RetroPlugApplication.*",
		"src/lsdj/**.h",
		"src/lsdj/**.cpp",
		"src/node/**.h",
		"src/node/**.cpp",
		"src/util/**.h",
		"src/util/**.cpp",
		"src/ui/**.h",
		"src/ui/**.cpp",
		"src/ecs/**.h",
		"src/ecs/**.cpp",
	}

	filter { "platforms:Emscripten" }
		disablewarnings { "character-conversion" }

	filter{}

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
