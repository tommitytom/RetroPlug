local iplug2 = {
	project = {}
}

local _config
local _s

function iplug2.init(configPath)
	_config = require(configPath or "config")
	_config.source = _config.source .. "/"
	_s = _config.source

	return iplug2
end

function iplug2.workspace(name)
	workspace(name)
		location ("build/".._ACTION)
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

	configuration { "windows" }
		disablewarnings { "4996", "4250", "4018", "4267", "4068", "4150" }
		defines {
			"NOMINMAX",
			"WIN32",
			"_CRT_SECURE_NO_WARNINGS",
			"_CRT_SECURE_NO_DEPRECATE",
			"_CRT_NONSTDC_NO_DEPRECATE"
		}

	configuration { "windows", "x64" }
		defines { "WIN64" }

	configuration {}
end

local function baseSetup()
	local g = _config.graphics

	defines {
		"SAMPLE_TYPE_FLOAT",
		"IPLUG_EDITOR=1",
		"IPLUG_DSP=1",
		"_CONSOLE"
	}

	includedirs {
		"config",
		"resources",
		_s.."iPlug",
		_s.."iPlug/Extras",
		_s.."IGraphics",
		_s.."IGraphics/Controls",
		_s.."IGraphics/Drawing",
		_s.."IGraphics/Platforms",
		_s.."IGraphics/Extras",
		_s.."WDL",
		_s.."Dependencies/IGraphics/STB"
	}

	files {
		"resources/resource.h",
		"resources/main.rc",
		"config/config.h",

		_s.."iPlug/*.h",
		_s.."iPlug/*.cpp",
		_s.."IGraphics/*.h",
		_s.."IGraphics/*.cpp",
		_s.."IGraphics/Controls/*.h",
		_s.."IGraphics/Controls/*.cpp",
	}

	if g.platform == "gl2" then
		includedirs {
			_s.."Dependencies/IGraphics/glad_GL2/include",
			_s.."Dependencies/IGraphics/glad_GL2/src"
		}

		defines { "IGRAPHICS_GL2" }
	end

	if g.platform == "gl3" then
		includedirs {
			_s.."Dependencies/IGraphics/glad_GL3/include",
			_s.."Dependencies/IGraphics/glad_GL3/src"
		}

		defines { "IGRAPHICS_GL3" }
	end

	if g.backend == "nanovg" then
		includedirs {
			_s.."Dependencies/IGraphics/NanoVG/src",
			_s.."Dependencies/IGraphics/NanoSVG/src"
		}

		defines { "IGRAPHICS_NANOVG" }

		files {_s.."IGraphics/Drawing/IGraphicsNanoVG.h" }
	end

	if g.vsync == true then
		defines { "IGRAPHICS_VSYNC" }
	end

	configuration { "windows" }
		files {
			_s.."IGraphics/Platforms/IGraphicsWin.cpp",
		}

		links {
			"Wininet",
			"Shlwapi"
		}

	configuration {}
end

function iplug2.project.app(name, fn)
	project(name)
	kind "WindowedApp"
	baseSetup()

	defines { "APP_API" }

	if _config.app.allowMultiple == true then
		defines { "APP_ALLOW_MULTIPLE_INSTANCES" }
	end

	includedirs {
		_s.."iPlug/APP",
		_s.."Dependencies/IPlug/RTAudio",
		_s.."Dependencies/IPlug/RTAudio/include",
		_s.."Dependencies/IPlug/RTMidi"
	}

	files {
		_s.."iPlug/APP/*.h",
		_s.."iPlug/APP/*.cpp",
		_s.."Dependencies/IPlug/RTAudio/RtAudio.cpp",
		_s.."Dependencies/IPlug/RTAudio/include/*.h",
		_s.."Dependencies/IPlug/RTAudio/include/*.cpp",
		_s.."Dependencies/IPlug/RtMidi/RtMidi.cpp"
	}

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

function iplug2.project.vst2(name, fn)
	project(name)
	kind "SharedLib"
	baseSetup()

	defines {
		"VST2_API",
		"VST_FORCE_DEPRECATED"
	}

	includedirs {
		_s.."iPlug/VST2",
		_s.."Dependencies/IPlug/VST2_SDK"
	}

	files {
		_s.."iPlug/VST2/*.h",
		_s.."iPlug/VST2/*.cpp"
	}

	configuration {}

	if fn then fn() end
end

function iplug2.project.vst3(name, fn)
	project(name)
	kind "SharedLib"
	baseSetup()

	defines {
		"VST3_API",
		"VST_FORCE_DEPRECATED"
	}

	includedirs {
		_s.."iPlug/VST3",
		_s.."Dependencies/IPlug/VST3_SDK"
	}

	files {
		_s.."iPlug/VST3/*.h",
		_s.."iPlug/VST3/*.cpp"
	}

	configuration {}

	if fn then fn() end
end

return iplug2
