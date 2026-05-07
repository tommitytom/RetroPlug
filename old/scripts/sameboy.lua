local SAMEBOY_DIR = "../thirdparty/SameBoy/"
local XXHASH_DIR = "../thirdparty/xxhash/"
local BOOTROM_DIR = "%{wks.location}/../../thirdparty/SameBoy/BootROMs"
local BOOTROM_OBJ = "%{cfg.objdir}/%{file.basename}.a"
local BOOTROM_BIN = "%{cfg.objdir}/%{file.basename}.bin"
local BOOTROM_HEADER = "%{wks.location}/../../src/generated/bootroms/%{file.basename}.h"
local BOOTROM_RES_DIR = "%{wks.location}/obj/%{cfg.platform}/%{cfg.buildcfg}/SameBoyBootRoms"

if _OPTIONS["emscripten"] == nil then

project "pb12"
	kind "ConsoleApp"
	language "C"
	toolset "clang"

	files { SAMEBOY_DIR .. "BootROMs/pb12.c" }

	configuration { "windows" }
		defines { "_CRT_SECURE_NO_WARNINGS" }
		includedirs { SAMEBOY_DIR .. "Windows" }

	configuration { "macosx" }
		--[[xcodebuildsettings {
			["MACOSX_DEPLOYMENT_TARGET"] = "10.14"
		}]]

		systemversion "10.14"

project "bin2h"
	kind "ConsoleApp"
	language "C++"
	characterset "Unicode"

	files { "../thirdparty/bin2h/main.cpp" }

project "SameBoyBootRoms"
	kind "Utility"
	dependson { "pb12", "bin2h" }

	files { SAMEBOY_DIR .. "BootROMs/**.asm" }

	prebuildcommands {
		'rgbgfx -Z -u -c embedded -o "%{cfg.objdir}/SameBoyLogo.2bpp" "' .. BOOTROM_DIR .. '/SameBoyLogo.png"',
		'"%{cfg.buildtarget.directory}/pb12" < "%{cfg.objdir}/SameBoyLogo.2bpp" > "%{cfg.objdir}/SameBoyLogo.pb12"'
	}

	filter ('files:' .. SAMEBOY_DIR .. 'BootROMs/**.asm')
		buildmessage '%{file.basename}.asm'

		buildcommands {
			'rgbasm -i "' .. BOOTROM_RES_DIR .. '" -i "' .. BOOTROM_DIR .. '" -o "' .. BOOTROM_OBJ .. '" "%{file.relpath}"',
			'rgblink -o "' .. BOOTROM_BIN .. '" "' .. BOOTROM_OBJ .. '"',
			'"%{cfg.buildtarget.directory}/bin2h" "' .. BOOTROM_BIN .. '" "' .. BOOTROM_HEADER .. '" -id=%{file.basename}'
		}

		buildoutputs { BOOTROM_OBJ, BOOTROM_BIN, BOOTROM_HEADER }

end

local function getVersion()
	local file = io.open("../thirdparty/SameBoy/version.mk", "r")
	if file == nil then
		error("Failed to detect SameBoy version: version.mk could not be opened")
	end

	local version = file:read()
	local st, en = version:find(":= ")

	if st == nil then
		error("Failed to detect SameBoy version: version.mk contains invalid data")
	end

	version = version:sub(en + 1)

	file:close()

	return version
end

project "SameBoy"
	kind "StaticLib"
	language "C"
	toolset "clang"

	defines { "GB_INTERNAL", "GB_DISABLE_TIMEKEEPING", [[GB_VERSION="]] .. getVersion() .. [["]] }

	sysincludedirs {
		SAMEBOY_DIR .. "Core",
		XXHASH_DIR
	}

	includedirs {
		SAMEBOY_DIR .. "Core",
		"../src",
		"../src/retroplug",
		"../src/generated/bootroms"
	}

	files {
		SAMEBOY_DIR .. "Core/**.h",
		SAMEBOY_DIR .. "Core/**.c",
		XXHASH_DIR .. "*.h",
		XXHASH_DIR .. "*.c",
		"../src/plugs/SameBoyPlug.h",
		"../src/plugs/SameBoyPlug.cpp",
	}

	disablewarnings { "int-in-bool-context", "unused-function", "unused-const-variable" }

	configuration { "windows" }
		dependson "SameBoyBootRoms"
		includedirs { SAMEBOY_DIR .. "Windows" }
		defines { "_CRT_SECURE_NO_WARNINGS" }
