local paths = dofile("../paths.lua")

local m = {}

local PUGL_DIR = paths.DEP_ROOT .. "pugl"

function m.include()
	includedirs {
		PUGL_DIR .. "/include",
		PUGL_DIR .. "/bindings/cpp/include"
	}

	defines {
		"PUGL_STATIC",
	}

	filter {}
end

function m.link()
	m.include()
	links { "pugl" }
end

function m.project()
	project "pugl"
		removeplatforms { "Emscripten" }
		kind "StaticLib"
		language "C++"

		m.include()

		files {
			PUGL_DIR .. "/include/**.h",
			PUGL_DIR .. "/src/attributes.h",
			PUGL_DIR .. "/src/common.c",
			PUGL_DIR .. "/src/internal.h",
			PUGL_DIR .. "/src/internal.c",
			PUGL_DIR .. "/src/macros.h",
			PUGL_DIR .. "/src/platform.h",
			PUGL_DIR .. "/src/types.h",
		}

		filter "system:windows"
			files {
				PUGL_DIR .. "/src/win.h",
				PUGL_DIR .. "/src/win.c",
				PUGL_DIR .. "/src/win_gl.c",
			}

			links {
				"user32",
				"shlwapi",
				"gdi32",
				"dwmapi",
				"opengl32",
			}

		filter "system:linux"
			files {
				PUGL_DIR .. "/src/x11.h",
				PUGL_DIR .. "/src/x11.c",
				PUGL_DIR .. "/src/x11_gl.c",
			}

		filter "system:macosx"
			files {
				PUGL_DIR .. "/src/mac.h",
				PUGL_DIR .. "/src/mac.c",
				PUGL_DIR .. "/src/mac_gl.c",
			}

		filter {}
end

return m
