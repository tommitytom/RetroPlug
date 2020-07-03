local iplug2 = "thirdparty/iPlug2/"

workspace "RetroPlug"
	location ("build/".._ACTION)
	configurations { "Debug", "Release" }
	platforms { "x64" }
	characterset "MBCS"
	language "C++"
	cppdialect "C++latest"

	flags { "MultiProcessorCompile" }

	defines {
		"SAMPLE_TYPE_FLOAT",
		"IGRAPHICS_VSYNC",
		"IGRAPHICS_NANOVG",
		"IGRAPHICS_GL2",
		"IPLUG_EDITOR=1",
		"IPLUG_DSP=1",
		"_CONSOLE"
	}

	includedirs {
		"src",
		"config",
		"resources",
		"thirdparty",
		"thirdparty/gainput/lib/include",
		"thirdparty/simplefilewatcher/include",
		"thirdparty/lua-5.3.5/src",
		"thirdparty/liblsdj/liblsdj/include/lsdj",
		"thirdparty/minizip",
		"thirdparty/sol",
		iplug2.."iPlug",
		iplug2.."iPlug/Extras",
		iplug2.."IGraphics",
		iplug2.."IGraphics/Controls",
		iplug2.."IGraphics/Drawing",
		iplug2.."IGraphics/Platforms",
		iplug2.."IGraphics/Extras",
		iplug2.."WDL",
		iplug2.."Dependencies/IGraphics/STB",
		iplug2.."Dependencies/IGraphics/glad_GL2/include",
		iplug2.."Dependencies/IGraphics/glad_GL2/src",
		iplug2.."Dependencies/IGraphics/NanoVG/src",
		iplug2.."Dependencies/IGraphics/NanoSVG/src",
		iplug2.."Dependencies/IPlug/RTAudio/include",
	}

	configuration { "Debug" }
		defines { "DEBUG", "_DEBUG" }
		symbols "On"
		libdirs { "thirdparty/lib/debug_x64" }

	configuration { "Release" }
		defines { "NDEBUG" }
		optimize "On"
		libdirs { "thirdparty/lib/release_x64" }

	configuration { "windows" }
		disablewarnings { "4996", "4250", "4018", "4267", "4068", "4150" }
		defines {
			"NOMINMAX",
			"WIN32",
			"WIN64",
			"_CRT_SECURE_NO_WARNINGS",
			"_CRT_SECURE_NO_DEPRECATE",
			"_CRT_NONSTDC_NO_DEPRECATE"
		}

project "App"
	kind "ConsoleApp"

	defines {
		"APP_API",
		"__WINDOWS_DS__",
		"__WINDOWS_MM__",
		"__WINDOWS_ASIO__",
		--"APP_ALLOW_MULTIPLE_INSTANCES"
	}

	includedirs {
		iplug2.."iPlug/APP",
		iplug2.."Dependencies/IPlug/RTAudio",
		iplug2.."Dependencies/IPlug/RTMidi",
	}

	files {
		iplug2.."IGraphics/*.h",
		iplug2.."IGraphics/*.cpp",
		iplug2.."IGraphics/Controls/*.h",
		iplug2.."IGraphics/Controls/*.cpp",
		iplug2.."IGraphics/Drawing/IGraphicsNanoVG.h",

		iplug2.."iPlug/*.h",
		iplug2.."iPlug/*.cpp",
		iplug2.."iPlug/APP/*.h",
		iplug2.."iPlug/APP/*.cpp",

		-- APP specific
		iplug2.."Dependencies/IPlug/RTAudio/RtAudio.cpp",
		iplug2.."Dependencies/IPlug/RTAudio/include/*.h",
		iplug2.."Dependencies/IPlug/RTAudio/include/*.cpp",
		iplug2.."Dependencies/IPlug/RtMidi/RtMidi.cpp",

		"thirdparty/liblsdj/liblsdj/include/lsdj/**.h",
		"thirdparty/liblsdj/liblsdj/src/**.c",

		"resources/resource.h",
		"resources/main.rc",
		"resources/dlls/**",
		"resources/fonts/**",

		"config/config.h",

		"src/**.h",
		"src/**.c",
		"src/**.cpp"
	}

	links {
		"lua",
		"simplefilewatcher",
		"minizip",
		"gainput",
	}

	filter { "files:src/luawrapper/**" }
    	buildoptions { "/bigobj" }

	configuration { "windows" }
		files {
			--iplug2.."IGraphics/Drawing/IGraphicsNanoVG.cpp",
			iplug2.."IGraphics/Platforms/IGraphicsWin.cpp",
		}

		links {
			"dsound",
			"xinput",
			"ws2_32",
			"Wininet",
			"Shlwapi"
		}
