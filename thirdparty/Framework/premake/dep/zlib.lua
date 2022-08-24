local ZLIB_DIR = "thirdparty/zlib"

local m = {}

function m.include()
	sysincludedirs { ZLIB_DIR }
	defines {
		"_LARGEFILE64_SOURCE=0",
		"N_FSEEKO=1"
	}
end

function m.link()
	m.include()
	links { "zlib" }
end

function m.project()
	project "zlib"
		kind "StaticLib"
		language "C"

		m.include()

		files {
			ZLIB_DIR .. "/crc32.h",
			ZLIB_DIR .. "/deflate.h",
			ZLIB_DIR .. "/gzguts.h",
			ZLIB_DIR .. "/inffast.h",
			ZLIB_DIR .. "/inffixed.h",
			ZLIB_DIR .. "/inflate.h",
			ZLIB_DIR .. "/inftrees.h",
			ZLIB_DIR .. "/zutil.h",
			ZLIB_DIR .. "/adler32.c",
			ZLIB_DIR .. "/compress.c",
			ZLIB_DIR .. "/crc32.c",
			ZLIB_DIR .. "/deflate.c",
			ZLIB_DIR .. "/gzclose.c",
			ZLIB_DIR .. "/gzlib.c",
			ZLIB_DIR .. "/gzread.c",
			ZLIB_DIR .. "/gzwrite.c",
			ZLIB_DIR .. "/inflate.c",
			ZLIB_DIR .. "/infback.c",
			ZLIB_DIR .. "/inftrees.c",
			ZLIB_DIR .. "/inffast.c",
			ZLIB_DIR .. "/trees.c",
			ZLIB_DIR .. "/uncompr.c",
			ZLIB_DIR .. "/zutil.c",
		}

		filter "system:windows"
			defines { "_WINDOWS" }
			disablewarnings { 4996, 4267 }

		filter "system:not windows"
			defines { 'HAVE_UNISTD_H' }
end

return m
