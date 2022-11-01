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

local function writeConfig(target)
	os.mkdir(target)

	local resRoot = paths.SRC_ROOT .. "plugin/resources/"
	local matches = os.matchfiles(resRoot .. "*")

	for _, v in ipairs(matches) do
		os.copyfile(v, target .. path.getname(v))
	end

	local file = io.open(target .. "config.h", "w+")
	file:write([[
		#define PLUG_NAME "FrameworkInstrument"
		#define PLUG_MFR "AcmeInc"
		#define PLUG_VERSION_HEX 0x00010000
		#define PLUG_VERSION_STR "1.0.0"
		#define PLUG_UNIQUE_ID 'PmBl'
		#define PLUG_MFR_ID 'Acme'
		#define PLUG_URL_STR "https://iplug2.github.io"
		#define PLUG_EMAIL_STR "spam@me.com"
		#define PLUG_COPYRIGHT_STR "Copyright 2020 Acme Inc"
		#define PLUG_CLASS_NAME FrameworkInstrument
		#define PLUG_USER_CLASS_NAME Granular

		#define BUNDLE_NAME "FrameworkInstrument"
		#define BUNDLE_MFR "AcmeInc"
		#define BUNDLE_DOMAIN "com"

		#define PLUG_CHANNEL_IO "0-2"
		#define SHARED_RESOURCES_SUBPATH "FrameworkInstrument"

		#define PLUG_LATENCY 0
		#define PLUG_TYPE 1
		#define PLUG_DOES_MIDI_IN 1
		#define PLUG_DOES_MIDI_OUT 1
		#define PLUG_DOES_MPE 1
		#define PLUG_DOES_STATE_CHUNKS 0
		#define PLUG_HAS_UI 1
		#define PLUG_WIDTH 1024
		#define PLUG_HEIGHT 669
		#define PLUG_FPS 60
		#define PLUG_SHARED_RESOURCES 0
		#define PLUG_HOST_RESIZE 0

		#define AUV2_ENTRY FrameworkInstrument_Entry
		#define AUV2_ENTRY_STR "FrameworkInstrument_Entry"
		#define AUV2_FACTORY FrameworkInstrument_Factory
		#define AUV2_VIEW_CLASS FrameworkInstrument_View
		#define AUV2_VIEW_CLASS_STR "FrameworkInstrument_View"

		#define AAX_TYPE_IDS 'IPI1', 'IPI2'
		#define AAX_PLUG_MFR_STR "Acme"
		#define AAX_PLUG_NAME_STR "FrameworkInstrument\nIPIS"
		#define AAX_DOES_AUDIOSUITE 0
		#define AAX_PLUG_CATEGORY_STR "Synth"

		#define VST3_SUBCATEGORY "Instrument|Synth"

		#define APP_NUM_CHANNELS 2
		#define APP_N_VECTOR_WAIT 0
		#define APP_MULT 1
		#define APP_COPY_AUV3 0
		#define APP_SIGNAL_VECTOR_SIZE 64

		#define ROBOTO_FN "Roboto-Regular.ttf"

	]])

	file:close()
end

function m.createApp(name)
	local baseName = name .. "-iPlug2"
	local fullName = baseName .. "-app"

	writeConfig(paths.BUILD_ROOT .. "generated/" .. baseName .. "/")

	project(fullName)
	kind "WindowedApp"

	filter { "system:windows", "configurations:Debug" }
		kind "ConsoleApp"

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
		"%{prj.location}/generated/" .. baseName
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
			"%{prj.location}/generated/" .. baseName .. "/main.rc",
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
