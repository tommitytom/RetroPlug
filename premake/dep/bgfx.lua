local util = dofile("../util.lua")

local m = {}

local BX_DIR = "thirdparty/bx";
local BIMG_DIR = "thirdparty/bimg";
local BGFX_DIR = "thirdparty/bgfx";

function m.includeBx()
	defines { "__STDC_FORMAT_MACROS", "BX_CONFIG_CRT_DIRECTORY_READER=0" }

	sysincludedirs {
		BX_DIR .. "/3rdparty",
		BX_DIR .. "/include"
	}
end

function m.includeBimg()
	sysincludedirs {
		BIMG_DIR .. "/include",
		BIMG_DIR .. "/3rdparty",
		BIMG_DIR .. "/3rdparty/astc-codec",
		BIMG_DIR .. "/3rdparty/astc-codec/include"
	}
end

function m.includeBgfx()
	sysincludedirs {
		BX_DIR .. "/include",
		BIMG_DIR .. "/include",
		BGFX_DIR .. "/include",
		BGFX_DIR .. "/3rdparty",
		BGFX_DIR .. "/3rdparty/dxsdk/include",
		BGFX_DIR .. "/3rdparty/khronos",
		BGFX_DIR .. "/tools",
		BGFX_DIR .. "/examples/common/nanovg",
	}

	defines {
		"BGFX_CONFIG_MULTITHREADED=0",
		-- "BGFX_CONFIG_TRANSIENT_VERTEX_BUFFER_SIZE=(6<<22)" -- Causes issues with emscripten?
	}

	configuration { "web" }
		defines { "BGFX_CONFIG_RENDERER_OPENGLES=30" }

	configuration { "windows" }
		defines { "BGFX_CONFIG_RENDERER_OPENGL=43" }

	filter "configurations:Debug"
		defines { "BGFX_CONFIG_DEBUG=1" }

	filter "configurations:Development"
		defines { "BGFX_CONFIG_DEBUG=1" }

	filter {}
end

function m.include()
	m.includeBx()
	m.includeBimg()
	m.includeBgfx()
end

function m.link()
	m.include()

	filter "system:windows"
		links { "Psapi" }
	filter "system:macosx"
		links { "QuartzCore.framework", "Metal.framework", "Cocoa.framework", "IOKit.framework", "CoreVideo.framework" }
	filter "system:linux"
		links { "dl", "GL", "pthread", "X11" }

	filter {}

	-- The order of these is important on linux!
	links { "bgfx", "bimg", "bx" }

	m.compat()
end

function m.bxProject()
	project "bx"
		kind "StaticLib"
		cppdialect "C++2a"
		exceptionhandling "Off"
		rtti "Off"

		m.includeBx()

		files {
			BX_DIR .. "/include/bx/*.h",
			BX_DIR .. "/include/bx/inline/*.inl",
			BX_DIR .. "/src/*.cpp"
		}
		excludes {
			BX_DIR .. "/src/amalgamated.cpp",
			BX_DIR .. "/src/crtnone.cpp"
		}

		m.compat()
end

function m.bimgProject()
	project "bimg"
		kind "StaticLib"
		cppdialect "C++2a"
		exceptionhandling "Off"
		rtti "Off"

		m.includeBx()
		m.includeBimg()

		files {
			BIMG_DIR .. "/include/bimg/*.h",
			BIMG_DIR .. "/src/image.cpp",
			BIMG_DIR .. "/src/image_gnf.cpp",
			BIMG_DIR .. "/src/image_decode.cpp",
			BIMG_DIR .. "/src/*.h",
			BIMG_DIR .. "/3rdparty/astc-codec/src/decoder/*.cc"
		}

		filter "system:linux"
			disablewarnings { "unknown-warning-option" }

		filter {}

		m.compat()
end

function m.bgfxProject()
	project "bgfx"
		kind "StaticLib"
		cppdialect "C++2a"
		exceptionhandling "Off"
		rtti "Off"

		m.includeBx()
		m.includeBimg()
		m.includeBgfx()

		files {
			BGFX_DIR .. "/include/bgfx/**.h",
			BGFX_DIR .. "/src/*.cpp",
			BGFX_DIR .. "/src/*.h",
			BGFX_DIR .. "/examples/common/nanovg/**.h",
			BGFX_DIR .. "/examples/common/nanovg/**.cpp",
			BGFX_DIR .. "/examples/common/nanovg/**.sc",
			BGFX_DIR .. "/3rdparty/stb/**.h",
		}
		excludes {
			BGFX_DIR .. "/src/amalgamated.cpp",
		}

		filter "action:vs*"
			excludes {
				BGFX_DIR .. "/src/glcontext_glx.cpp", -- OpenGL Linux / BSD
				-- BGFX_DIR .. "/src/glcontext_egl.cpp", -- GLES all platforms
				-- BGFX_DIR .. "/src/glcontext_wgl.cpp", -- OpenGL windows
				-- BGFX_DIR .. "/src/glcontext_eagl.cpp", -- GLES iOS
				-- BGFX_DIR .. "/src/glcontext_html5.cpp", -- GLES + emscripten
				-- BGFX_DIR .. "/src/glcontext_nsgl.cpp", -- OpenGL + OSX
			}

		filter "system:macosx"
			files {
				BGFX_DIR .. "/src/*.mm",
			}

		filter {} 

		links { "bx", "bimg" }

		m.compat()
end

function m.compat()
	filter "action:vs*"
		--includedirs { path.join(BX_DIR, "include/compat/msvc") }
	filter { "system:windows", "action:gmake" }
		includedirs { path.join(BX_DIR, "include/compat/mingw") }
	filter { "system:macosx" }
		sysincludedirs { path.join(BX_DIR, "include/compat/osx") }
		buildoptions { "-x objective-c++" }
	filter {}
end

return m
