local MINIZIP_DIR = "../thirdparty/minizip"

project "minizip"
	kind "StaticLib"
	language "C"

	defines {
		"WIN32",
		"_WINDOWS",
		"HAVE_STDINT_H",
		"HAVE_INTTYPES_H",
		"NO_FSEEKO",
		"_CRT_SECURE_NO_DEPRECATE",
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
		"_CRT_NONSTDC_NO_DEPRECATE",
		"_CRT_SECURE_NO_WARNINGS",
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
		MINIZIP_DIR .. "/**.h",
		MINIZIP_DIR .. "/**.c",
		MINIZIP_DIR .. "/zlib-ng/*.h",
		MINIZIP_DIR .. "/zlib-ng/*.c",
		MINIZIP_DIR .. "/zlib-ng/arch/x86/*.h",
		MINIZIP_DIR .. "/zlib-ng/arch/x86/*.c"
	}

	excludes {
		MINIZIP_DIR .. "/minizip.c",
		MINIZIP_DIR .. "/minigzip.c",
		MINIZIP_DIR .. "/miniunz.c",
		MINIZIP_DIR .. "/mz_strm_libcomp.*",
		MINIZIP_DIR .. "/mz_crypt_*.c",
		MINIZIP_DIR .. "/mz_strm_os_*.c",
		MINIZIP_DIR .. "/mz_os_*.c",
		MINIZIP_DIR .. "/test/**",

		MINIZIP_DIR .. "/zlib-ng/arch/arm/**",
		MINIZIP_DIR .. "/zlib-ng/arch/s390/**",
		MINIZIP_DIR .. "/zlib-ng/build/**",
		MINIZIP_DIR .. "/zlib-ng/cmake/**",
		MINIZIP_DIR .. "/zlib-ng/test/**",
		MINIZIP_DIR .. "/zlib-ng/tools/**"
	}

	configuration { "Debug" }
		defines { "ZLIB_DEBUG" }

	configuration { "windows" }
		disablewarnings { "4267", "4244", "4311" }

		files {
			MINIZIP_DIR .. "/mz_crypt_win32.c",
			MINIZIP_DIR .. "/mz_strm_os_win32.c",
			MINIZIP_DIR .. "/mz_os_win32.c"
		}