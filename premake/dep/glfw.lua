local m = {}

local GLFW_DIR = "thirdparty/glfw"

function m.include()
	sysincludedirs {
		GLFW_DIR .. "/include"
	}

	filter "system:windows"
		defines "_GLFW_WIN32"

	filter "system:linux"
		defines "_GLFW_X11"

	filter "system:macosx"
		defines "_GLFW_COCOA"

	filter {}
end

function m.link()
	m.include()
	links { "glfw" }
end

function m.project()
	project "glfw"
	kind "StaticLib"
	language "C"

	m.include()

	files {
		GLFW_DIR .. "/include/GLFW/*.h",
		GLFW_DIR .. "/src/context.c",
		GLFW_DIR .. "/src/egl_context.*",
		GLFW_DIR .. "/src/init.c",
		GLFW_DIR .. "/src/input.c",
		GLFW_DIR .. "/src/internal.h",
		GLFW_DIR .. "/src/monitor.c",
		GLFW_DIR .. "/src/osmesa_context.*",
		GLFW_DIR .. "/src/vulkan.c",
		GLFW_DIR .. "/src/window.c",
	}

	filter "system:windows"
		files {
			GLFW_DIR .. "/src/win32_*.*",
			GLFW_DIR .. "/src/wgl_context.*"
		}

	filter "system:linux"
		files {
			GLFW_DIR .. "/src/glx_context.*",
			GLFW_DIR .. "/src/linux*.*",
			GLFW_DIR .. "/src/posix*.*",
			GLFW_DIR .. "/src/x11*.*",
			GLFW_DIR .. "/src/xkb*.*"
		}

	filter "system:macosx"
		files {
			GLFW_DIR .. "/src/cocoa_*.*",
			GLFW_DIR .. "/src/posix_thread.h",
			GLFW_DIR .. "/src/nsgl_context.h",
			GLFW_DIR .. "/src/egl_context.h",
			GLFW_DIR .. "/src/osmesa_context.h",

			GLFW_DIR .. "/src/posix_thread.c",
			GLFW_DIR .. "/src/nsgl_context.m",
			GLFW_DIR .. "/src/egl_context.c",
			GLFW_DIR .. "/src/nsgl_context.m",
			GLFW_DIR .. "/src/osmesa_context.c"
		}

	filter {}
end

return m
