local MINIZIP_DIR = "thirdparty/minizip-ng"
local LZMA_DIR = MINIZIP_DIR .. "/lib/liblzma/src"

local m = {}

function m.include()
	defines {
		-- liblzma
		"HAVE_VISIBILITY=0",
		"HAVE_CHECK_CRC32",
		"HAVE_CHECK_CRC64",
		"HAVE_CHECK_SHA256",
		"HAVE_DECODERS",
		"HAVE_DECODER_ARM",
		"HAVE_DECODER_ARMTHUMB",
		"HAVE_DECODER_DELTA",
		"HAVE_DECODER_IA64",
		"HAVE_DECODER_LZMA1",
		"HAVE_DECODER_LZMA2",
		"HAVE_DECODER_POWERPC",
		"HAVE_DECODER_SPARC",
		"HAVE_DECODER_X86",
		"HAVE_ENCODERS",
		"HAVE_ENCODER_ARM",
		"HAVE_ENCODER_ARMTHUMB",
		"HAVE_ENCODER_DELTA",
		"HAVE_ENCODER_IA64",
		"HAVE_ENCODER_LZMA1",
		"HAVE_ENCODER_LZMA2",
		"HAVE_ENCODER_POWERPC",
		"HAVE_ENCODER_SPARC",
		"HAVE_ENCODER_X86",

		"HAVE_STDBOOL_H",
		"HAVE__BOOL",
		"HAVE_STDINT_H",
		"HAVE_INTTYPES_H",
		"HAVE___BUILTIN_ASSUME_ALIGNED",

		"HAVE_IMMINTRIN_H",
		"HAVE__MM_MOVEMASK_EPI8",
		"HAVE_MF_BT2",
		"HAVE_MF_BT3",
		"HAVE_MF_BT4",
		"HAVE_MF_HC3",
		"HAVE_MF_HC4",

		"TUKLIB_FAST_UNALIGNED_ACCESS",
		"TUKLIB_SYMBOL_PREFIX=lzma_",

		"PACKAGE_BUGREPORT=\"lasse.collin@tukaani.org\"",
		"PACKAGE_NAME=\"XZ Utils\"",
		"PACKAGE_URL=\"https://tukaani.org/xz/\"",

		-- zstd
		--"ZSTD_MULTITHREAD",
		--"ZSTD_HEAPMODE=0",
		--"ZSTD_LEGACY_SUPPORT=0",
		--"XXH_NAMESPACE=ZSTD_",

		-- zlib
		"NO_FSEEKO",

		-- zlib-ng
		--[["ZLIBNG_VERNUM=0x2020",
		"HAVE_BUILTIN_CTZ",
		"HAVE_BUILTIN_CTZLL",
		"HAVE_SYS_SDT_H",
		"HAVE_VISIBILITY_HIDDEN",
		"HAVE_VISIBILITY_INTERNAL",
		"UNALIGNED64_OK",
		"UNALIGNED_OK",
		"WITH_GZFILEOP",
		"X86_AVX2",
		"X86_AVX2_ADLER32",
		"X86_AVX_CHUNKSET",
		"X86_FEATURES",
		"X86_PCLMULQDQ_CRC",
		"X86_SSE2",
		"X86_SSE2_CHUNKSET",
		"X86_SSE2_SLIDEHASH",
		"X86_SSE42_CMP_STR",
		"X86_SSE42_CRC_HASH",
		"X86_SSE42_CRC_INTRIN",
		"X86_SSSE3",
		"X86_SSSE3_ADLER32",]]

		-- minizip
		"HAVE_ZLIB",
		--"HAVE_BZIP2",
		"HAVE_LZMA",
		"LZMA_API_STATIC",
		--"HAVE_ZSTD",
		--"MZ_ZIP_SIGNING",
		--"HAVE_PKCRYPT",
		--"HAVE_WZAES",
	}

	sysincludedirs {
		MINIZIP_DIR,
		LZMA_DIR,
		LZMA_DIR .. "/common",
		LZMA_DIR .. "/liblzma/common",
		LZMA_DIR .. "/liblzma/api",
		LZMA_DIR .. "/liblzma/check",
		LZMA_DIR .. "/liblzma/lz",
		LZMA_DIR .. "/liblzma/lzma",
		LZMA_DIR .. "/liblzma/delta",
		LZMA_DIR .. "/liblzma/rangecoder",
		LZMA_DIR .. "/liblzma/simple",
	}
end

function m.link()
	m.include()
	links { "zlib", "minizip" }
end

function m.project()
	project "minizip"
		kind "StaticLib"
		language "C"

		m.include()

		files {
			MINIZIP_DIR .. "/*.h",
			MINIZIP_DIR .. "/*.c",
		}

		files {
			LZMA_DIR .. "/common/mythread.h",
			LZMA_DIR .. "/common/sysdefs.h",
			LZMA_DIR .. "/common/tuklib_common.h",
			LZMA_DIR .. "/common/tuklib_config.h",
			LZMA_DIR .. "/common/tuklib_cpucores.c",
			LZMA_DIR .. "/common/tuklib_cpucores.h",
			LZMA_DIR .. "/common/tuklib_integer.h",
			LZMA_DIR .. "/common/tuklib_physmem.c",
			LZMA_DIR .. "/common/tuklib_physmem.h",
			LZMA_DIR .. "/liblzma/api/lzma.h",
			LZMA_DIR .. "/liblzma/api/lzma/base.h",
			LZMA_DIR .. "/liblzma/api/lzma/bcj.h",
			LZMA_DIR .. "/liblzma/api/lzma/block.h",
			LZMA_DIR .. "/liblzma/api/lzma/check.h",
			LZMA_DIR .. "/liblzma/api/lzma/container.h",
			LZMA_DIR .. "/liblzma/api/lzma/delta.h",
			LZMA_DIR .. "/liblzma/api/lzma/filter.h",
			LZMA_DIR .. "/liblzma/api/lzma/hardware.h",
			LZMA_DIR .. "/liblzma/api/lzma/index.h",
			LZMA_DIR .. "/liblzma/api/lzma/index_hash.h",
			LZMA_DIR .. "/liblzma/api/lzma/lzma12.h",
			LZMA_DIR .. "/liblzma/api/lzma/stream_flags.h",
			LZMA_DIR .. "/liblzma/api/lzma/version.h",
			LZMA_DIR .. "/liblzma/api/lzma/vli.h",
			LZMA_DIR .. "/liblzma/check/check.c",
			LZMA_DIR .. "/liblzma/check/check.h",
			LZMA_DIR .. "/liblzma/check/crc32_fast.c",
			LZMA_DIR .. "/liblzma/check/crc32_table.c",
			LZMA_DIR .. "/liblzma/check/crc32_table_be.h",
			LZMA_DIR .. "/liblzma/check/crc32_table_le.h",
			LZMA_DIR .. "/liblzma/check/crc64_fast.c",
			LZMA_DIR .. "/liblzma/check/crc64_table.c",
			LZMA_DIR .. "/liblzma/check/crc64_table_be.h",
			LZMA_DIR .. "/liblzma/check/crc64_table_le.h",
			LZMA_DIR .. "/liblzma/check/crc_macros.h",
			LZMA_DIR .. "/liblzma/check/sha256.c",
			LZMA_DIR .. "/liblzma/common/alone_decoder.c",
			LZMA_DIR .. "/liblzma/common/alone_decoder.h",
			LZMA_DIR .. "/liblzma/common/alone_encoder.c",
			LZMA_DIR .. "/liblzma/common/auto_decoder.c",
			LZMA_DIR .. "/liblzma/common/block_buffer_decoder.c",
			LZMA_DIR .. "/liblzma/common/block_buffer_encoder.c",
			LZMA_DIR .. "/liblzma/common/block_buffer_encoder.h",
			LZMA_DIR .. "/liblzma/common/block_decoder.c",
			LZMA_DIR .. "/liblzma/common/block_decoder.h",
			LZMA_DIR .. "/liblzma/common/block_encoder.c",
			LZMA_DIR .. "/liblzma/common/block_encoder.h",
			LZMA_DIR .. "/liblzma/common/block_header_decoder.c",
			LZMA_DIR .. "/liblzma/common/block_header_encoder.c",
			LZMA_DIR .. "/liblzma/common/block_util.c",
			LZMA_DIR .. "/liblzma/common/common.c",
			LZMA_DIR .. "/liblzma/common/common.h",
			LZMA_DIR .. "/liblzma/common/easy_buffer_encoder.c",
			LZMA_DIR .. "/liblzma/common/easy_decoder_memusage.c",
			LZMA_DIR .. "/liblzma/common/easy_encoder.c",
			LZMA_DIR .. "/liblzma/common/easy_encoder_memusage.c",
			LZMA_DIR .. "/liblzma/common/easy_preset.c",
			LZMA_DIR .. "/liblzma/common/easy_preset.h",
			LZMA_DIR .. "/liblzma/common/file_info.c",
			LZMA_DIR .. "/liblzma/common/filter_buffer_decoder.c",
			LZMA_DIR .. "/liblzma/common/filter_buffer_encoder.c",
			LZMA_DIR .. "/liblzma/common/filter_common.c",
			LZMA_DIR .. "/liblzma/common/filter_common.h",
			LZMA_DIR .. "/liblzma/common/filter_decoder.c",
			LZMA_DIR .. "/liblzma/common/filter_decoder.h",
			LZMA_DIR .. "/liblzma/common/filter_encoder.c",
			LZMA_DIR .. "/liblzma/common/filter_encoder.h",
			LZMA_DIR .. "/liblzma/common/filter_flags_decoder.c",
			LZMA_DIR .. "/liblzma/common/filter_flags_encoder.c",
			LZMA_DIR .. "/liblzma/common/hardware_cputhreads.c",
			LZMA_DIR .. "/liblzma/common/hardware_physmem.c",
			LZMA_DIR .. "/liblzma/common/index.c",
			LZMA_DIR .. "/liblzma/common/index.h",
			LZMA_DIR .. "/liblzma/common/index_decoder.c",
			LZMA_DIR .. "/liblzma/common/index_decoder.h",
			LZMA_DIR .. "/liblzma/common/index_encoder.c",
			LZMA_DIR .. "/liblzma/common/index_encoder.h",
			LZMA_DIR .. "/liblzma/common/index_hash.c",
			LZMA_DIR .. "/liblzma/common/memcmplen.h",
			LZMA_DIR .. "/liblzma/common/outqueue.c",
			LZMA_DIR .. "/liblzma/common/outqueue.h",
			LZMA_DIR .. "/liblzma/common/stream_buffer_decoder.c",
			LZMA_DIR .. "/liblzma/common/stream_buffer_encoder.c",
			LZMA_DIR .. "/liblzma/common/stream_decoder.c",
			LZMA_DIR .. "/liblzma/common/stream_decoder.h",
			LZMA_DIR .. "/liblzma/common/stream_encoder.c",
			LZMA_DIR .. "/liblzma/common/stream_encoder_mt.c",
			LZMA_DIR .. "/liblzma/common/stream_flags_common.c",
			LZMA_DIR .. "/liblzma/common/stream_flags_common.h",
			LZMA_DIR .. "/liblzma/common/stream_flags_decoder.c",
			LZMA_DIR .. "/liblzma/common/stream_flags_encoder.c",
			LZMA_DIR .. "/liblzma/common/vli_decoder.c",
			LZMA_DIR .. "/liblzma/common/vli_encoder.c",
			LZMA_DIR .. "/liblzma/common/vli_size.c",
			LZMA_DIR .. "/liblzma/delta/delta_common.c",
			LZMA_DIR .. "/liblzma/delta/delta_common.h",
			LZMA_DIR .. "/liblzma/delta/delta_decoder.c",
			LZMA_DIR .. "/liblzma/delta/delta_decoder.h",
			LZMA_DIR .. "/liblzma/delta/delta_encoder.c",
			LZMA_DIR .. "/liblzma/delta/delta_encoder.h",
			LZMA_DIR .. "/liblzma/delta/delta_private.h",
			LZMA_DIR .. "/liblzma/lz/lz_decoder.c",
			LZMA_DIR .. "/liblzma/lz/lz_decoder.h",
			LZMA_DIR .. "/liblzma/lz/lz_encoder.c",
			LZMA_DIR .. "/liblzma/lz/lz_encoder.h",
			LZMA_DIR .. "/liblzma/lz/lz_encoder_hash.h",
			LZMA_DIR .. "/liblzma/lz/lz_encoder_hash_table.h",
			LZMA_DIR .. "/liblzma/lz/lz_encoder_mf.c",
			LZMA_DIR .. "/liblzma/lzma/fastpos.h",
			LZMA_DIR .. "/liblzma/lzma/fastpos_table.c",
			LZMA_DIR .. "/liblzma/lzma/lzma2_decoder.c",
			LZMA_DIR .. "/liblzma/lzma/lzma2_decoder.h",
			LZMA_DIR .. "/liblzma/lzma/lzma2_encoder.c",
			LZMA_DIR .. "/liblzma/lzma/lzma2_encoder.h",
			LZMA_DIR .. "/liblzma/lzma/lzma_common.h",
			LZMA_DIR .. "/liblzma/lzma/lzma_decoder.c",
			LZMA_DIR .. "/liblzma/lzma/lzma_decoder.h",
			LZMA_DIR .. "/liblzma/lzma/lzma_encoder.c",
			LZMA_DIR .. "/liblzma/lzma/lzma_encoder.h",
			LZMA_DIR .. "/liblzma/lzma/lzma_encoder_optimum_fast.c",
			LZMA_DIR .. "/liblzma/lzma/lzma_encoder_optimum_normal.c",
			LZMA_DIR .. "/liblzma/lzma/lzma_encoder_presets.c",
			LZMA_DIR .. "/liblzma/lzma/lzma_encoder_private.h",
			LZMA_DIR .. "/liblzma/rangecoder/price.h",
			LZMA_DIR .. "/liblzma/rangecoder/price_table.c",
			LZMA_DIR .. "/liblzma/rangecoder/range_common.h",
			LZMA_DIR .. "/liblzma/rangecoder/range_decoder.h",
			LZMA_DIR .. "/liblzma/rangecoder/range_encoder.h",
			LZMA_DIR .. "/liblzma/simple/arm.c",
			LZMA_DIR .. "/liblzma/simple/armthumb.c",
			LZMA_DIR .. "/liblzma/simple/ia64.c",
			LZMA_DIR .. "/liblzma/simple/powerpc.c",
			LZMA_DIR .. "/liblzma/simple/simple_coder.c",
			LZMA_DIR .. "/liblzma/simple/simple_coder.h",
			LZMA_DIR .. "/liblzma/simple/simple_decoder.c",
			LZMA_DIR .. "/liblzma/simple/simple_decoder.h",
			LZMA_DIR .. "/liblzma/simple/simple_encoder.c",
			LZMA_DIR .. "/liblzma/simple/simple_encoder.h",
			LZMA_DIR .. "/liblzma/simple/simple_private.h",
			LZMA_DIR .. "/liblzma/simple/sparc.c",
			LZMA_DIR .. "/liblzma/simple/x86.c"
		}

		excludes {
			MINIZIP_DIR .. "/minizip.c",
			MINIZIP_DIR .. "/minigzip.c",
			MINIZIP_DIR .. "/miniunz.c",
			MINIZIP_DIR .. "/mz_strm_bzip.c",
			MINIZIP_DIR .. "/mz_strm_zstd.c",
			MINIZIP_DIR .. "/mz_strm_libcomp.*",
			MINIZIP_DIR .. "/mz_crypt_*.c",
			MINIZIP_DIR .. "/mz_strm_os_*.c",
			MINIZIP_DIR .. "/mz_os_*.c"
		}

		filter { "configurations:Debug" }
			defines { "ZLIB_DEBUG" }

		filter { "system:not macosx" }
			sysincludedirs {
				MINIZIP_DIR .. "/lib/zlib",
			}

		filter "system:windows"
			disablewarnings { "4267", "4244", "4311" }

			defines {
				"WIN32",
				"_WINDOWS",
				"_CRT_SECURE_NO_WARNINGS",

				-- liblzma
				"MYTHREAD_VISTA",
			}

			files {
				MINIZIP_DIR .. "/mz_crypt_win32.c",
				MINIZIP_DIR .. "/mz_strm_os_win32.c",
				MINIZIP_DIR .. "/mz_os_win32.c",
			}

		filter "system:macosx"
			defines {
				"_POSIX_C_SOURCE=200112L",
				"_GNU_SOURCE",

				-- liblzma
				"MYTHREAD_POSIX"
			}

			files {
				MINIZIP_DIR .. "/mz_crypt_apple.c",
				MINIZIP_DIR .. "/mz_strm_os_posix.c",
				MINIZIP_DIR .. "/mz_os_posix.c"
			}

			--[[filter { "system:macosx", "files:**/crc_folding.c" }
				buildoptions { "-mssse3", "-msse4", "-mpclmul" }

			filter { "system:macosx", "files:**/*_avx.c" }
				buildoptions { "-mavx2" }

			filter { "system:macosx", "files:**/*_sse.c" }
				buildoptions { "-msse4" }

			filter { "system:macosx", "files:**/*_sse3.c" }
				buildoptions { "-msse3" }]]

		filter "system:linux"
			defines {
				"MYTHREAD_POSIX",
				"MZ_ZIP_NO_CRYPTO"
			}
			files {
				MINIZIP_DIR .. "/mz_strm_os_posix.c",
				MINIZIP_DIR .. "/mz_os_posix.c",
			}
end

return m
