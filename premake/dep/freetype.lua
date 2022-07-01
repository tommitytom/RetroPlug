local FREETYPE_DIR = "thirdparty/freetype"
local FREETYPEGL_DIR = "thirdparty/freetype-gl"

local m = {}

function m.include()
	sysincludedirs {
		FREETYPE_DIR .. "/include",
		FREETYPEGL_DIR,
	}

	defines {
		"FT2_BUILD_LIBRARY",
	}
end

function m.source()
	m.include()

	files {
		FREETYPE_DIR .. "/src/autofit/autofit.c",
		FREETYPE_DIR .. "/src/base/ftbase.c",
		FREETYPE_DIR .. "/src/base/ftbbox.c",
		FREETYPE_DIR .. "/src/base/ftbdf.c",
		FREETYPE_DIR .. "/src/base/ftbitmap.c",
		FREETYPE_DIR .. "/src/base/ftcid.c",
		FREETYPE_DIR .. "/src/base/ftfstype.c",
		FREETYPE_DIR .. "/src/base/ftgasp.c",
		FREETYPE_DIR .. "/src/base/ftglyph.c",
		FREETYPE_DIR .. "/src/base/ftgxval.c",
		FREETYPE_DIR .. "/src/base/ftinit.c",
		FREETYPE_DIR .. "/src/base/ftmm.c",
		FREETYPE_DIR .. "/src/base/ftotval.c",
		FREETYPE_DIR .. "/src/base/ftpatent.c",
		FREETYPE_DIR .. "/src/base/ftpfr.c",
		FREETYPE_DIR .. "/src/base/ftstroke.c",
		FREETYPE_DIR .. "/src/base/ftsynth.c",
		FREETYPE_DIR .. "/src/base/fttype1.c",
		FREETYPE_DIR .. "/src/base/ftwinfnt.c",
		FREETYPE_DIR .. "/src/bdf/bdf.c",
		FREETYPE_DIR .. "/src/bzip2/ftbzip2.c",
		FREETYPE_DIR .. "/src/cache/ftcache.c",
		FREETYPE_DIR .. "/src/cff/cff.c",
		FREETYPE_DIR .. "/src/cid/type1cid.c",
		FREETYPE_DIR .. "/src/gzip/ftgzip.c",
		FREETYPE_DIR .. "/src/lzw/ftlzw.c",
		FREETYPE_DIR .. "/src/pcf/pcf.c",
		FREETYPE_DIR .. "/src/pfr/pfr.c",
		FREETYPE_DIR .. "/src/psaux/psaux.c",
		FREETYPE_DIR .. "/src/pshinter/pshinter.c",
		FREETYPE_DIR .. "/src/psnames/psnames.c",
		FREETYPE_DIR .. "/src/raster/raster.c",
		FREETYPE_DIR .. "/src/sdf/sdf.c",
		FREETYPE_DIR .. "/src/sfnt/sfnt.c",
		FREETYPE_DIR .. "/src/smooth/smooth.c",
		FREETYPE_DIR .. "/src/svg/svg.c",
		FREETYPE_DIR .. "/src/truetype/truetype.c",
		FREETYPE_DIR .. "/src/type1/type1.c",
		FREETYPE_DIR .. "/src/type42/type42.c",
		FREETYPE_DIR .. "/src/winfonts/winfnt.c",

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

	filter "system:windows"
		files {
			FREETYPE_DIR .. "/builds/windows/ftsystem.c",
			FREETYPE_DIR .. "/builds/windows/ftdebug.c"
		}

	filter "system:linux"
		files {
			FREETYPE_DIR .. "/builds/unix/ftsystem.c"
		}
end

function m.link()
	m.include()
	links { "freetype" }
end

function m.project()
	project "freetype"
		kind "StaticLib"
		language "C++"
		m.source()
end

return m
