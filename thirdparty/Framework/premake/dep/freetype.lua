local paths = dofile("../paths.lua")

local FREETYPE_DIR = paths.DEP_ROOT .. "freetype"

local m = {}

function m.include()
	includedirs {
		paths.DEP_ROOT .. "zlib",
		FREETYPE_DIR .. "/include"
	}

	defines {
		"FT2_BUILD_LIBRARY", "FT_CONFIG_OPTION_SYSTEM_ZLIB",
	}

	filter { "system:linux" }
		defines {
			"HAVE_FCNTL_H",
			"HAVE_UNISTD_H"
		}

	filter {}
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
	}

	filter "system:windows"
		files {
			FREETYPE_DIR .. "/builds/windows/ftsystem.c",
			FREETYPE_DIR .. "/builds/windows/ftdebug.c"
		}

	filter "system:linux"
		files {
			FREETYPE_DIR .. "/builds/unix/ftsystem.c",
			FREETYPE_DIR .. "/src/base/ftdebug.c"
		}
end

function m.link()
	m.include()

	filter { "platforms:not Emscripten" }
		links { "freetype", "zlib" }

	filter { "system:linux" }
		links { "dl", "pthread" }

	filter{}
end

function m.project()
	project "freetype"
		kind "StaticLib"
		language "C++"
		m.source()

		filter { "action:vs*" }
			disablewarnings { 4101, 4267, 4305, 4996, 4018, 4244 }

		filter {}
end

return m
