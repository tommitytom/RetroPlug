local SAMEBOY_DIR = "../thirdparty/SameBoy/"
local BOOTROM_DIR = "%{wks.location}/../../thirdparty/SameBoy/BootROMs"
local BOOTROM_OBJ = "%{cfg.objdir}/%{file.basename}.a"
local BOOTROM_BIN = "%{cfg.objdir}/%{file.basename}.bin"
local BOOTROM_HEADER = "%{wks.location}/../../src/generated/bootroms/%{file.basename}.h"
local BOOTROM_RES_DIR = "%{wks.location}/obj/%{cfg.platform}/%{cfg.buildcfg}/SameBoyBootRoms"

project "pb12"
	kind "ConsoleApp"
	language "C"
	toolset "clang"

	defines { "_CRT_SECURE_NO_WARNINGS" }
	includedirs { SAMEBOY_DIR .. "Windows" }
	files { SAMEBOY_DIR .. "BootROMs/pb12.c" }

project "bin2h"
	kind "ConsoleApp"
	language "C++"
	characterset "Unicode"

	files { "../thirdparty/bin2h/main.cpp" }

project "SameBoyBootRoms"
	kind "Utility"
	dependson { "pb12", "bin2h" }

	files { SAMEBOY_DIR .. "BootROMs/**.asm" }

	configuration { "windows" }
		prebuildcommands {
			'"%{cfg.buildtarget.directory}/pb12.exe" < "' .. BOOTROM_DIR .. '/SameBoyLogo.png" > "%{cfg.objdir}/SameBoyLogo.pb12"'
		}

	filter ('files:' .. SAMEBOY_DIR .. 'BootROMs/**.asm')
		buildmessage '%{file.basename}.asm'

		buildcommands {
			'rgbasm -i "' .. BOOTROM_RES_DIR .. '" -i "' .. BOOTROM_DIR .. '" -o "' .. BOOTROM_OBJ .. '" "%{file.relpath}"',
			'rgblink -o "' .. BOOTROM_BIN .. '" "' .. BOOTROM_OBJ .. '"',
			'"%{cfg.buildtarget.directory}/bin2h.exe" "' .. BOOTROM_BIN .. '" "' .. BOOTROM_HEADER .. '" -id=%{file.basename}'
		}

		buildoutputs { BOOTROM_OBJ, BOOTROM_BIN, BOOTROM_HEADER }

project "SameBoy"
	kind "StaticLib"
	language "C"
	toolset "clang"
	dependson "SameBoyBootRoms"

	defines { "GB_INTERNAL", "_CRT_SECURE_NO_WARNINGS" }

	includedirs {
		SAMEBOY_DIR .. "Core",
		SAMEBOY_DIR .. "Windows",
		"../src/generated/bootroms"
	}

	files {
		SAMEBOY_DIR .. "Core/**.h",
		SAMEBOY_DIR .. "Core/**.c",
		SAMEBOY_DIR .. "retroplug/**.h",
		SAMEBOY_DIR .. "retroplug/**.c"
	}
