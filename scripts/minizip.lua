local MINIZIP_DIR = "../thirdparty/minizip"

project "minizip"
	kind "StaticLib"
	language "C"

	defines {
		"HAVE_STDINT_H",
		"HAVE_INTTYPES_H",
		"NO_FSEEKO",
		"MZ_ZIP_SIGNING",
		"HAVE_PKCRYPT",
		"HAVE_WZAES",
		"HAVE_ZLIB",
		"HAVE_BZIP2",
		"BZ_NO_STDIO",
		"HAVE_CONFIG_H",
		"HAVE_LIMITS_H",
		"HAVE_STRING_H",
		"HAVE_MEMORY_H",
		"HAVE_STDBOOL_H",
		"HAVE_IMMINTRIN_H",
		"HAVE_LZMA",
		"LZMA_API_STATIC",
		"ZLIB_COMPAT",
		"WITH_GZFILEOP",
		"UNALIGNED_OK",
		"X86_CPUID",
		"X86_AVX2",
		"X86_SSE42_CRC_HASH",
		"X86_SSE42_CRC_INTRIN",
		"X86_QUICK_STRATEGY",
		"X86_SSE42_CMP_STR",
		"X86_SSE2",
		"X86_PCLMULQDQ_CRC",
		"USE_ZLIB"
	}

	includedirs {
		MINIZIP_DIR,
		MINIZIP_DIR .. "/lib/bzip2",
		MINIZIP_DIR .. "/lib/liblzma",
		MINIZIP_DIR .. "/lib/liblzma/api",
		MINIZIP_DIR .. "/lib/liblzma/check",
		MINIZIP_DIR .. "/lib/liblzma/common",
		MINIZIP_DIR .. "/lib/liblzma/lz",
		MINIZIP_DIR .. "/lib/liblzma/lzma",
		MINIZIP_DIR .. "/lib/liblzma/rangecoder",
		MINIZIP_DIR .. "/zlib-ng"
	}

	files {
		MINIZIP_DIR .. "/*.h",
		MINIZIP_DIR .. "/*.c",
		MINIZIP_DIR .. "/lib/**.h",
		MINIZIP_DIR .. "/lib/**.c",
		MINIZIP_DIR .. "/zlib-ng/*.h",
		MINIZIP_DIR .. "/zlib-ng/*.c"
	}

	excludes {
		MINIZIP_DIR .. "/minizip.c",
		MINIZIP_DIR .. "/minigzip.c",
		MINIZIP_DIR .. "/miniunz.c",
		MINIZIP_DIR .. "/mz_strm_libcomp.*",
		MINIZIP_DIR .. "/mz_crypt_*.c",
		MINIZIP_DIR .. "/mz_strm_os_*.c",
		MINIZIP_DIR .. "/mz_os_*.c"
	}

	configuration { "not emscripten" }
		files {
			MINIZIP_DIR .. "/zlib-ng/arch/x86/*.h",
			MINIZIP_DIR .. "/zlib-ng/arch/x86/*.c"
		}

	configuration { "Debug" }
		defines { "ZLIB_DEBUG" }

	filter "system:windows"
		disablewarnings { "4267", "4244", "4311" }

		defines {
			"WIN32",
			"_WINDOWS",
		}

		files {
			MINIZIP_DIR .. "/mz_crypt_win32.c",
			MINIZIP_DIR .. "/mz_strm_os_win32.c",
			MINIZIP_DIR .. "/mz_os_win32.c"
		}

	filter "system:macosx"
		files {
			MINIZIP_DIR .. "/mz_crypt_apple.c",
			MINIZIP_DIR .. "/mz_strm_os_posix.c",
			MINIZIP_DIR .. "/mz_os_posix.c"
		}

	filter "system:linux"
		files {
			MINIZIP_DIR .. "/mz_strm_os_posix.c",
			MINIZIP_DIR .. "/mz_os_posix.c"
		}

		buildoptions { "-msimd128", "-msse2" }
