solution "retroplug-dep"
	location ("build/".._ACTION)

	configurations { "Debug", "Release" }
	platforms { "x64" }

	flags { "MultiProcessorCompile" }

	configuration "Debug"
		defines { "DEBUG" }
		symbols "On"

	configuration "Release"
		defines { "NDEBUG" }
		optimize "On"

	-- Set up targets
	configuration { "windows", "x64", "Debug" }
		targetdir "lib/debug_x64"

	configuration { "windows", "x64", "Release" }
		targetdir "lib/release_x64"

	configuration { "linux", "gmake2" }
		buildoptions { "-Wfatal-errors" }

	configuration { "windows" }
		defines { "WIN32", "_WINDOWS" }
		characterset "MBCS"
		staticruntime "off"

	configuration "vs*"
		systemversion "latest"

	project "gainput"
		kind "StaticLib"
		language "C++"

		includedirs { "gainput/lib/include" }
		files { "gainput/lib/include/gainput/**.h", "gainput/lib/source/gainput/**.cpp" }

	project "lua"
		kind "StaticLib"
		language "C"

		includedirs { "lua-5.3.5/src" }
		files { "lua-5.3.5/src/**.h", "lua-5.3.5/src/**.c" }
		excludes { "lua-5.3.5/src/lua.c", "lua-5.3.5/src/luac.c" }

	project "simplefilewatcher"
		kind "StaticLib"
		language "C++"

		includedirs { "simplefilewatcher/include" }
		files { "simplefilewatcher/include/**.h", "simplefilewatcher/source/**.cpp" }
		excludes { "lua-5.3.5/src/lua.c", "lua-5.3.5/src/luac.c" }

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

		includedirs { "minizip", "zipper/zlib-ng" }

		files { "minizip/**.h", "minizip/**.c" }
		excludes { "minizip/minizip.c", "minizip/miniunz.c" }

		files { "minizip/zlib-ng/*.h", "minizip/zlib-ng/*.c" }
		files { "minizip/zlib-ng/arch/x86/*.h", "minizip/zlib-ng/arch/x86/*.c" }
		excludes { "minizip/zlib-ng/arch", "minizip/zlib-ng/build", "minizip/zlib-ng/cmake", "minizip/zlib-ng/test", "minizip/zlib-ng/tools" }

		configuration { "Debug" }
			defines { "ZLIB_DEBUG" }
