--local configgen = require("configgen")
local util = require("util")

local iplug2 = {
	project = {}
}

local _config -- Config loaded from file
local _p -- Root path of iPlug2 (for convenience)
local _name -- The name of the plugin (for convenience)

function iplug2.init(configPath)
	local scriptPath = util.scriptPath()

	_config = require(configPath or "config")
	_name = _config.plugin.name
	_p = scriptPath:sub(1, #scriptPath - 4)

	iplug2.path = _p

	--configgen.generate(_config)

	return iplug2
end

function iplug2.workspace(name)
	workspace(name)
		location ("build/" .. _ACTION)
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

	--configuration { "emscripten" }
		--defines {}

	configuration {}
end

local function projectBase(targetName, name)
	name = name or targetName
	project(_name .. "-" .. name)
	targetname(util.getTargetName(_name, name))

	-- Added to all projects --

	defines {
		"SAMPLE_TYPE_FLOAT",
		"IPLUG_EDITOR=1",
		"IPLUG_DSP=1",
		"_CONSOLE"
	}

	sysincludedirs {
		_p.."iPlug"
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
		"config/config.h",

		_p.."iPlug/*.h",
		_p.."iPlug/*.cpp",
		_p.."IGraphics/*.h",
		_p.."IGraphics/*.cpp",
		_p.."IGraphics/Controls/*.h",
		_p.."IGraphics/Controls/*.cpp",
	}

	local config = util.flattenConfig(_config.config, targetName)

	-- Graphics settings --

	local g = config.graphics
	local gdep = _p.."Dependencies/IGraphics/"

	if g.platform == "gl2" then
		configuration { "not emscripten" }
			includedirs { gdep.."glad_GL2/include", gdep.."glad_GL2/src" }
			defines { "IGRAPHICS_GL2" }
		configuration { "emscripten" }
			--includedirs { gdep.."glad_GL2/include", gdep.."glad_GL2/src" }
			defines { "IGRAPHICS_GLES2" }
		configuration {}
	end

	if g.platform == "gl3" then
		includedirs { gdep.."glad_GL3/include", gdep.."glad_GL3/src" }
		defines { "IGRAPHICS_GL3" }
	end

	if g.backend == "nanovg" then
		includedirs { gdep.."NanoVG/src", gdep.."NanoSVG/src" }
		defines { "IGRAPHICS_NANOVG" }
		files {
			_p.."IGraphics/Drawing/IGraphicsNanoVG.h",
		}

		configuration { "windows" }
			files {
				--_p.."IGraphics/Drawing/IGraphicsNanoVG.cpp",
			}

		configuration { "macosx" }
			files {
				_p.."IGraphics/Drawing/IGraphicsNanoVG_src.m"
			}

		filter { "system:macosx", "files:**/IGraphicsNanoVG_src.m" }
			buildoptions { "-fobjc-arc" }

		filter {}
	end

	if g.vsync == true then
		defines { "IGRAPHICS_VSYNC" }
	end

	-- Configure post build scripts --

	if config.outDir32 then
		configuration { "x86" }
			postbuildcommands { "{COPY} %{cfg.buildtarget.abspath} " .. config.outDir32 }
	end

	if config.outDir64 then
		configuration { "x64" }
			postbuildcommands { "{COPY} %{cfg.buildtarget.abspath} " .. config.outDir64 }
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
			"resources/resource.h",
			"resources/main.rc",
			_p.."IGraphics/Platforms/IGraphicsWin.cpp",
		}

		links {
			"Wininet",
			"Shlwapi"
		}

	configuration { "windows", "x64" }
		defines { "WIN64" }

	-- Mac specific settings --

	configuration { "macosx" }
		includedirs {
			_p.."WDL/swell"
		}

		files {
			_p.."iPlug/*.mm",
			_p.."IGraphics/Platforms/IGraphicsMac.mm",
			_p.."IGraphics/Platforms/IGraphicsMac_view.mm",
			_p.."IGraphics/Platforms/IGraphicsCoreText.mm",
		}

	configuration { "emscripten" }
		files {
			_p.."IGraphics/Platforms/IGraphicsWeb.cpp",
		}

	configuration { "Debug" }
		defines { "DEVELOPMENT=1" }

	configuration { "Release" }
		defines { "RELEASE=1" }

	configuration {}

	return config
end

function iplug2.project.app(fn, name)
	if _OPTIONS["emscripten"] ~= nil then return end

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
		_p.."Dependencies/IPlug/RtMidi/RtMidi.cpp"
	}

	if config.debugConsole == true then
		--configuration { "Debug" }
			--kind "ConsoleApp"
	end

	configuration { "windows" }
		defines {
			"__WINDOWS_DS__",
			"__WINDOWS_MM__",
			"__WINDOWS_ASIO__"
		}

		files {
			_p.."Dependencies/IPlug/RTAudio/include/*.h",
			_p.."Dependencies/IPlug/RTAudio/include/*.cpp",
		}

		links { "dsound" }

	configuration { "macosx" }
		defines {
			"__MACOSX_CORE__",
			"SWELL_COMPILED",
			"SWELL_CLEANUP_ON_UNLOAD",
			"OBJC_PREFIX=v" .. _name,
			"SWELL_APP_PREFIX=Swell_v" .. _name
		}

		files {
			_p.."WDL/swell/*.h",
			_p.."WDL/swell/*.mm",
			_p.."WDL/swell/swell-ini.cpp",
			_p.."WDL/swell/swell.cpp",
		}
		excludes {
			_p.."WDL/swell/swell-modstub.mm",
		}

		linkoptions {
			"-framework AppKit",
			"-framework CoreMIDI",
			"-framework CoreAudio",
			"-framework Cocoa",
			"-framework Carbon",
			"-framework CoreFoundation",
			"-framework CoreData",
			"-framework Foundation",
			"-framework CoreServices",
			"-framework Metal",
			"-framework MetalKit",
			"-framework QuartzCore",
			"-framework OpenGL",
			"-framework IOKit",
			"-framework Security"
		}

	configuration {}

	if fn then fn() end
end

function iplug2.project.vst2(fn, name)
	if _OPTIONS["emscripten"] ~= nil then return end

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

	configuration {}

	if fn then fn() end
end

function iplug2.project.vst3(fn, name)
	if _OPTIONS["emscripten"] ~= nil then return end

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

function iplug2.project.auv3(fn, name)
	if _OPTIONS["emscripten"] ~= nil then return end

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

	configuration { "macosx" }
		links {
			"AudioToolbox.framework",
			"AVFoundation.framework",
			"CoreAudio.framework",
			"CoreAudioKit.framework"
		}

	configuration {}

	if fn then fn() end
end

function iplug2.project.wam(fn, name)
	if _OPTIONS["emscripten"] == nil then return end

	local config = projectBase("wam", name)
	kind "SharedLib"

	defines {
		--"VST2_API",
		--"VST_FORCE_DEPRECATED"
	}

	includedirs {
		_p.."iPlug/WEB",
		--_p.."Dependencies/IPlug/VST2_SDK"
	}

	files {
		_p.."iPlug/WEB/*.h",
		_p.."iPlug/WEB/*.cpp"
	}

	configuration {}

	if fn then fn() end
end

return iplug2
