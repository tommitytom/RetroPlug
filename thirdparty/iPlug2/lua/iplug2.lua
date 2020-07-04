--local configgen = require("configgen")
local util = require("util")

local iplug2 = {
	project = {}
}

local _config
local _p
local _name

local BUILD_DIR = "build/" .. _ACTION

function iplug2.init(configPath)
	local scriptPath = util.scriptPath()

	_config = require(configPath or "config")
	_name = _config.plugin.name
	_p = scriptPath:sub(1, #scriptPath - 4)

	--configgen.generate(_config)

	return iplug2
end

function iplug2.workspace(name)
	workspace(name)
		location (BUILD_DIR)
		configurations { "Debug", "Release", "Tracer" }
		language "C++"
		flags { "MultiProcessorCompile" }

	configuration { "Debug" }
		defines { "DEBUG", "_DEBUG" }
		symbols "On"

	configuration { "Release" }
		defines { "NDEBUG" }
		optimize "On"

	configuration { "Tracer" }
		defines { "NDEBUG", "TRACER_BUILD" }
		optimize "On"

	configuration {}
end

local function projectBase(targetName, name)
	name = name or targetName

	local config = util.flattenConfig(_config.config, targetName)
	local g = config.graphics

	project(_name .. "-" .. name)
	targetname(util.getTargetName(_name, name))

	-- Added to all projects --

	defines {
		"SAMPLE_TYPE_FLOAT",
		"IPLUG_EDITOR=1",
		"IPLUG_DSP=1",
		"_CONSOLE"
	}

	includedirs {
		"config",
		"resources",
		_p.."iPlug",
		_p.."iPlug/Extras",
		_p.."IGraphics",
		_p.."IGraphics/Controls",
		_p.."IGraphics/Drawing",
		_p.."IGraphics/Platforms",
		_p.."IGraphics/Extras",
		_p.."Dependencies/IGraphics/STB",
		_p.."WDL"
	}

	files {
		"resources/resource.h",
		"resources/main.rc",
		"config/config.h",

		_p.."iPlug/*.h",
		_p.."iPlug/*.cpp",
		_p.."IGraphics/*.h",
		_p.."IGraphics/*.cpp",
		_p.."IGraphics/Controls/*.h",
		_p.."IGraphics/Controls/*.cpp",
	}

	-- Graphics settings --
	local gdep = _p.."Dependencies/IGraphics/"

	if g.platform == "gl2" then
		includedirs {
			gdep.."glad_GL2/include",
			gdep.."glad_GL2/src"
		}

		defines { "IGRAPHICS_GL2" }
	end

	if g.platform == "gl3" then
		includedirs {
			gdep.."glad_GL3/include",
			gdep.."glad_GL3/src"
		}

		defines { "IGRAPHICS_GL3" }
	end

	if g.backend == "nanovg" then
		includedirs {
			gdep.."NanoVG/src",
			gdep.."NanoSVG/src"
		}

		defines { "IGRAPHICS_NANOVG" }

		files { _p.."IGraphics/Drawing/IGraphicsNanoVG.h" }
	end

	if g.vsync == true then
		defines { "IGRAPHICS_VSYNC" }
	end

	-- Window specific settings --

	configuration { "windows" }
		disablewarnings { "4996", "4250", "4018", "4267", "4068", "4150" }
		defines {
			"WIN32",
			"NOMINMAX",
			"_CRT_SECURE_NO_WARNINGS",
			"_CRT_SECURE_NO_DEPRECATE",
			"_CRT_NONSTDC_NO_DEPRECATE"
		}

		files {
			_p.."IGraphics/Platforms/IGraphicsWin.cpp",
		}

		links {
			"Wininet",
			"Shlwapi"
		}

	configuration { "windows", "x64" }
		defines { "WIN64" }

	configuration {}

	return config
end

function iplug2.project.app(fn, name)
	local config = projectBase("app", name)
	kind "WindowedApp"

	defines { "APP_API" }

	if config.allowMultiple == true then
		defines { "APP_ALLOW_MULTIPLE_INSTANCES" }
	end

	includedirs {
		_p.."iPlug/APP",
		_p.."Dependencies/IPlug/RTAudio",
		_p.."Dependencies/IPlug/RTAudio/include",
		_p.."Dependencies/IPlug/RTMidi"
	}

	files {
		_p.."iPlug/APP/*.h",
		_p.."iPlug/APP/*.cpp",
		_p.."Dependencies/IPlug/RTAudio/RtAudio.cpp",
		_p.."Dependencies/IPlug/RTAudio/include/*.h",
		_p.."Dependencies/IPlug/RTAudio/include/*.cpp",
		_p.."Dependencies/IPlug/RtMidi/RtMidi.cpp"
	}

	if config.debugConsole == true then
		configuration { "Debug" }
			kind "ConsoleApp"
	end

	configuration { "windows" }
		defines {
			"__WINDOWS_DS__",
			"__WINDOWS_MM__",
			"__WINDOWS_ASIO__"
		}

		links { "dsound" }

	configuration {}

	if fn then fn() end
end

function iplug2.project.vst2(fn, name)
	local config = projectBase("vst2", name)
	kind "SharedLib"

	defines {
		"VST2_API",
		"VST_FORCE_DEPRECATED"
	}

	includedirs {
		_p.."iPlug/VST2",
		_p.."Dependencies/IPlug/VST2_SDK"
	}

	files {
		_p.."iPlug/VST2/*.h",
		_p.."iPlug/VST2/*.cpp"
	}

	local outDir = config.outDir64
	if outDir then
		local fileName = util.getTargetName(_name, name or "vst2")
		local sourceFile = "%{cfg.buildtarget.abspath}"
		local targetFile = outDir .. "/" .. fileName
		postbuildcommands {
			"{COPY} %{cfg.buildtarget.abspath} " .. outDir
		}
	end

	configuration {}

	if fn then fn() end
end

function iplug2.project.vst3(fn, name)
	projectBase("vst3", name)
	kind "SharedLib"

	defines {
		"VST3_API",
		"VST_FORCE_DEPRECATED"
	}

	includedirs {
		_p.."iPlug/VST3",
		_p.."Dependencies/IPlug/VST3_SDK"
	}

	files {
		_p.."iPlug/VST3/*.h",
		_p.."iPlug/VST3/*.cpp"
	}

	configuration {}

	if fn then fn() end
end

return iplug2
