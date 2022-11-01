local paths = dofile("../paths.lua")
local bgfx = dofile("bgfx.lua")

local _p = paths.DEP_ROOT .. "iPlug2/"

local m = {}

function m.include()
	defines {
		"SAMPLE_TYPE_FLOAT",
		"IPLUG_EDITOR=1",
		"IPLUG_DSP=1",
		"_CONSOLE",
		"IGRAPHICS_FRAMEWORK",
		--"IGRAPHICS_NANOVG",
		--"IGRAPHICS_GL2"
	}

	filter { "configurations:Debug" }
		defines { "DEVELOPMENT=1" }

	filter { "configurations:Release" }
		defines { "RELEASE=1" }

	filter { "system:windows", "platforms:x64" }
		defines { "WIN64" }

	filter {}

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
		_p.."Dependencies/IGraphics/NanoSVG/src",
		_p.."Dependencies/IGraphics/NanoVG/src",
		_p.."Dependencies/IGraphics/glad_GL2/include",
		_p.."Dependencies/IGraphics/glad_GL2/src",
		_p.."WDL"
	}

	bgfx.includeBgfx()

	filter { "system:windows" }
		disablewarnings { "4996", "4250", "4018", "4267", "4068", "4150" }
		defines {
			"WIN32",
			"NOMINMAX",
			"_CRT_SECURE_NO_WARNINGS",
			"_CRT_SECURE_NO_DEPRECATE",
			"_CRT_NONSTDC_NO_DEPRECATE"
		}

	filter { "system:macosx" }
		includedirs {
			_p.."WDL/swell"
		}

	filter {}
end

function m.source()
	m.include()

	files {
		--"config/config.h",

		_p.."iPlug/*.h",
		_p.."iPlug/*.cpp",
		_p.."IGraphics/*.h",
		_p.."IGraphics/*.cpp",
		_p.."IGraphics/Drawing/IGraphicsFramework.h",
		_p.."IGraphics/Drawing/IGraphicsFramework.cpp",
		_p.."IGraphics/Controls/*.h",
		_p.."IGraphics/Controls/*.cpp",
		--_p.."IGraphics/Extras/*.h",
		--_p.."IGraphics/Extras/*.cpp",

		_p.."Dependencies/IGraphics/NanoVG/src",
		_p.."Dependencies/IGraphics/glad_GL2/src",
	}

	filter { "system:windows" }
		files {
			_p.."IGraphics/Platforms/IGraphicsWin.cpp",
		}

	filter { "system:macosx" }
		files {
			_p.."iPlug/*.mm",
			_p.."IGraphics/Platforms/IGraphicsMac.h",
			_p.."IGraphics/Platforms/IGraphicsMac.mm",
			_p.."IGraphics/Platforms/IGraphicsMac_view.h",
			_p.."IGraphics/Platforms/IGraphicsMac_view.mm",
			_p.."IGraphics/Platforms/IGraphicsCoreText.h",
			_p.."IGraphics/Platforms/IGraphicsCoreText.mm",
		}

	filter {}
end

function m.link()
	m.include()
	links { "iPlug2" }

	filter { "system:windows" }
		links {
			"Wininet",
			"Shlwapi"
		}

	filter {}
end

function m.project()
	project "iPlug2"
		kind "StaticLib"

		m.source()

		--filter "system:windows"
			--disablewarnings { "4334", "4098", "4244" }
end

function m.createApp(name)
	local fullName = name .. "-iPlug2-app"

	project(fullName)
	kind "WindowedApp"

	--filter { "system:windows", "configurations:Debug" }
		--kind "ConsoleApp"

	filter {}

	defines { "APP_API" }

	m.include()

	--if config.allowMultiple == true then
	--	defines { "APP_ALLOW_MULTIPLE_INSTANCES" }
	--end

	includedirs {
		_p.."iPlug/APP",
		_p.."Dependencies/IPlug/RTAudio",
		_p.."Dependencies/IPlug/RTAudio/include",
		_p.."Dependencies/IPlug/RTMidi",
		"%{prj.location}/generated/" .. fullName
	}

	files {
		_p.."iPlug/APP/*.h",
		_p.."iPlug/APP/*.cpp",
		_p.."Dependencies/IPlug/RTAudio/RtAudio.cpp",
		_p.."Dependencies/IPlug/RtMidi/RtMidi.cpp"
	}

	filter { "system:windows" }
		defines {
			"__WINDOWS_DS__",
			"__WINDOWS_MM__",
			"__WINDOWS_ASIO__"
		}

		files {
			_p.."Dependencies/IPlug/RTAudio/include/*.h",
			_p.."Dependencies/IPlug/RTAudio/include/*.cpp",
			"%{prj.location}/generated/" .. fullName .. "/main.rc",
		}

		links { "dsound", "winmm", "Comctl32" }

	filter { "system:macosx" }
		defines {
			"__MACOSX_CORE__",
			"SWELL_COMPILED",
			"SWELL_CLEANUP_ON_UNLOAD",
			"OBJC_PREFIX=v" .. name,
			"SWELL_APP_PREFIX=Swell_v" .. name
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

	filter {}

	m.link()
end

return m
