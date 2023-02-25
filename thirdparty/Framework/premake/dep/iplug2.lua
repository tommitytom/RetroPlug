local paths = dofile("../paths.lua")
local bgfx = dofile("bgfx.lua")
local semver = dofile("semver.lua")

local _p = paths.DEP_ROOT .. "iPlug2/"
local VST3_DEP_PATH = _p .. "Dependencies/IPlug/VST3_SDK/"

local m = {}

function m.include()
	defines {
		"SAMPLE_TYPE_FLOAT",
		"IPLUG_EDITOR=1",
		"IPLUG_DSP=1",
		"_CONSOLE",
		"IGRAPHICS_FRAMEWORK",
		"IGRAPHICS_GL3"
	}

	filter { "configurations:Debug" }
		defines { "DEVELOPMENT=1" }

	filter { "configurations:Release" }
		defines { "RELEASE=1" }

	filter { "system:windows", "platforms:x64" }
		defines { "WIN64" }

	filter {}

	includedirs {
		_p.."iPlug"
	}

	includedirs {
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
		_p.."Dependencies/IGraphics/glad_GL3/include",
		_p.."Dependencies/IGraphics/glad_GL3/src",
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
		_p.."iPlug/*.h",
		_p.."iPlug/*.cpp",
		_p.."IGraphics/*.h",
		_p.."IGraphics/*.cpp",
		_p.."IGraphics/Drawing/IGraphicsFramework.h",
		_p.."IGraphics/Drawing/IGraphicsFramework.cpp",
		_p.."IGraphics/Controls/*.h",
		_p.."IGraphics/Controls/*.cpp",

		--_p.."Dependencies/IGraphics/glad_GL3/src/**.c",
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
			"Shlwapi",
			"Opengl32"
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

local util = dofile("../util.lua")

local function generateConfig(settings, target)
	local function copyFields(target, source)
		for k, v in pairs(source) do
			if type(v) ~= "table" then
				target[k] = v
			end
		end
	end

	local function convertBools(tab)
		for k, v in pairs(tab) do
			if type(v) == "boolean" then
				if v == true then tab[k] = 1 else tab[k] = 0 end
			end
		end
	end

	os.mkdir(target)

	local resRoot = paths.SRC_ROOT .. "plugin/resources/"
	local matches = os.matchfiles(resRoot .. "*")

	for _, v in ipairs(matches) do
		os.copyfile(v, target .. path.getname(v))
	end

	local v = semver(settings.version)
	local data = {
		versionHex = string.format("0x%.8x", (v.major << 16) | (v.minor << 8) | (v.patch & 0xFF)),
		channelIo = tostring(settings.audio.inputs) .. "-" .. tostring(settings.audio.outputs),
		year = os.date("%Y")
	}

	copyFields(data, settings)
	copyFields(data, settings.audio)
	copyFields(data, settings.graphics)
	copyFields(data, settings.plugin)
	convertBools(data)

	local s = util.interp([[// !! WARNING - THIS FILE IS GENERATED !!

#pragma once

#define PLUG_NAME "${name}"
#define PLUG_MFR "${author}"
#define PLUG_VERSION_HEX ${versionHex}
#define PLUG_VERSION_STR "${version}"
#define PLUG_UNIQUE_ID '${uniqueId}'
#define PLUG_MFR_ID '${authorId}'
#define PLUG_URL_STR "${url}"
#define PLUG_EMAIL_STR "${email}"
#define PLUG_COPYRIGHT_STR "Copyright ${year} ${author}"
#define PLUG_CLASS_NAME FrameworkInstrument
#define SHARED_RESOURCES_SUBPATH "${name}"

#define BUNDLE_NAME "${name}"
#define BUNDLE_MFR "${author}"
#define BUNDLE_DOMAIN "com"

#define PLUG_CHANNEL_IO "${channelIo}"

#define PLUG_LATENCY ${latency}
#define PLUG_TYPE 1
#define PLUG_DOES_MIDI_IN ${midiIn}
#define PLUG_DOES_MIDI_OUT ${midiOut}
#define PLUG_DOES_MPE 1
#define PLUG_DOES_STATE_CHUNKS ${stateChunks}
#define PLUG_HAS_UI 1
#define PLUG_WIDTH ${width}
#define PLUG_HEIGHT ${height}
#define PLUG_FPS ${fps}
#define PLUG_SHARED_RESOURCES ${sharedResources}
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY ${name}_Entry
#define AUV2_ENTRY_STR "${name}_Entry"
#define AUV2_FACTORY ${name}_Factory
#define AUV2_VIEW_CLASS ${name}_View
#define AUV2_VIEW_CLASS_STR "${name}_View"

#define AAX_TYPE_IDS 'EFN1', 'EFN2'
#define AAX_PLUG_MFR_STR "${author}"
#define AAX_PLUG_NAME_STR "${name}\nIPIS"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Synth"

#define VST3_SUBCATEGORY "Instrument"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_RESIZABLE 0
#define APP_SIGNAL_VECTOR_SIZE 64
]], data)

	local configTarget = target .. "config.h"
	--print("Writing config to " .. configTarget)
	local file = io.open(configTarget, "w+")

	if file == nil then
		error("Failed to write config file")
	end

	file:write(s)
	file:close()
end

local function projectBase(config, pluginType)
	if type(config) == "string" then
		local file = io.open(config, "r")
		if file == nil then
			error("Failed to load config from " .. config)
		end

		local fun, err = load(file:read("*all"))
		if err then error(err) end
		config = fun()
	end

	local baseName = config.name .. "-iPlug2"
	local fullName = baseName .. "-" .. pluginType

	generateConfig(config, paths.PROJECT_BUILD_ROOT .. "generated/" .. baseName .. "/")

	project(fullName)
		m.include()

		includedirs {
			paths.SRC_ROOT .. "plugin",
			"%{prj.location}/generated/" .. baseName
		}

		files {
			paths.SRC_ROOT .. "plugin/*"
		}

		filter { "system:windows" }
			files {
				"%{prj.location}/generated/" .. baseName .. "/main.rc",
			}

			links { "Comctl32" }

		filter {}

		m.link()

	return config
end

function m.createVst2(config)
	projectBase(config, "vst2")
	kind "SharedLib"

	defines {
		"VST2_API",
		"VST_FORCE_DEPRECATED"
	}

	includedirs {
		_p.."iPlug/VST2",
		_p.."Dependencies/IPlug/VST2_SDK",
	}

	files {
		_p.."iPlug/VST2/*.h",
		_p.."iPlug/VST2/*.cpp"
	}

	targetdir "C:\\vst64"
end

function m.createVst3(config)
	projectBase(config, "vst3")
	kind "SharedLib"
	targetextension ".vst3"

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
		_p.."iPlug/VST3/*.cpp",
		VST3_DEP_PATH .. "base/**.h",
		VST3_DEP_PATH .. "base/**.cpp",
		VST3_DEP_PATH .. "pluginterfaces/base/**.h",
		VST3_DEP_PATH .. "pluginterfaces/base/**.cpp",
		VST3_DEP_PATH .. "public.sdk/source/common/commoniids.cpp",
		VST3_DEP_PATH .. "public.sdk/source/common/memorystream.*",
		VST3_DEP_PATH .. "public.sdk/source/common/pluginview.*",
		VST3_DEP_PATH .. "public.sdk/source/main/dllmain.cpp",
		VST3_DEP_PATH .. "public.sdk/source/main/pluginfactory.cpp",
		VST3_DEP_PATH .. "public.sdk/source/vst/vstaudioeffect.*",
		VST3_DEP_PATH .. "public.sdk/source/vst/vstbus.*",
		VST3_DEP_PATH .. "public.sdk/source/vst/vstcomponent.*",
		VST3_DEP_PATH .. "public.sdk/source/vst/vstcomponentbase.*",
		VST3_DEP_PATH .. "public.sdk/source/vst/vstinitiids.cpp",
		VST3_DEP_PATH .. "public.sdk/source/vst/vstparameters.*",
		VST3_DEP_PATH .. "public.sdk/source/vst/vstsinglecomponenteffect.*",
	}
end

function m.createApp(config)
	config = projectBase(config, "app")
	kind "WindowedApp"

	filter { "system:windows", "configurations:Debug" }
		kind "ConsoleApp"

	filter {}

	defines { "APP_API" }

	--if config.allowMultiple == true then
	--	defines { "APP_ALLOW_MULTIPLE_INSTANCES" }
	--end

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

	filter { "system:windows" }
		defines {
			"__WINDOWS_DS__",
			"__WINDOWS_MM__",
			"__WINDOWS_ASIO__"
		}

		files {
			_p.."Dependencies/IPlug/RTAudio/include/*.h",
			_p.."Dependencies/IPlug/RTAudio/include/*.cpp",
		}

		links { "dsound", "winmm" }

	filter { "system:macosx" }
		defines {
			"__MACOSX_CORE__",
			"SWELL_COMPILED",
			"SWELL_CLEANUP_ON_UNLOAD",
			"OBJC_PREFIX=v" .. config.name,
			"SWELL_APP_PREFIX=Swell_v" .. config.name
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
end

return m
