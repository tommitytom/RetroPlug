local paths = dofile("../paths.lua")

local FREETYPE_DIR = paths.DEP_ROOT .. "freetype"
local FREETYPEGL_DIR = paths.DEP_ROOT .. "freetype-gl"

local m = {}

function m.include()
	includedirs {
		FREETYPE_DIR .. "/include",
		FREETYPEGL_DIR,
	}
end

function m.source()
	m.include()

	files {
		FREETYPEGL_DIR .. "/distance-field.h",
		FREETYPEGL_DIR .. "/distance-field.c",
		FREETYPEGL_DIR .. "/edtaa3func.h",
		FREETYPEGL_DIR .. "/edtaa3func.c",
		FREETYPEGL_DIR .. "/ftgl-utils.h",
		FREETYPEGL_DIR .. "/ftgl-utils.c",
		FREETYPEGL_DIR .. "/texture-font.h",
		FREETYPEGL_DIR .. "/texture-font.c",
		FREETYPEGL_DIR .. "/texture-atlas.h",
		FREETYPEGL_DIR .. "/texture-atlas.c",
		FREETYPEGL_DIR .. "/utf8-utils.h",
		FREETYPEGL_DIR .. "/utf8-utils.c",
		FREETYPEGL_DIR .. "/vector.h",
		FREETYPEGL_DIR .. "/vector.c",
	}
end

function m.link()
	m.include()
	links { "freetype-gl" }
end

function m.project()
	project "freetype-gl"
		kind "StaticLib"
		language "C"
		m.source()

		filter { "action:vs*" }
			disablewarnings { 4101, 4267, 4305, 4996, 4018, 4244 }

		filter {}
end

return m
