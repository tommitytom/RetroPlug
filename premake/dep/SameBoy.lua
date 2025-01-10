local m = {}

local SCRIPT_ROOT = debug.getinfo(1).source:match("@?(.*/)")

local repoRoot = SCRIPT_ROOT .. "../../"

local SAMEBOY_DIR = repoRoot .. "thirdparty/SameBoy"
local BOOTROM_DIR = repoRoot .. "thirdparty/SameBoy/BootROMs"
local BOOTROM_OBJ = "%{cfg.objdir}/%{file.basename}.a"
local BOOTROM_BIN = "%{cfg.objdir}/%{file.basename}.bin"
local BOOTROM_HEADER = "%{wks.location}/../../src/generated/bootroms/%{file.basename}.h"
local BOOTROM_RES_DIR = "%{wks.location}/obj/%{cfg.platform}/%{cfg.buildcfg}/SameBoyBootRoms"

local function getVersion()
	local file = io.open(SAMEBOY_DIR .. "/version.mk", "r")
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

function m.include()
	externalincludedirs { SAMEBOY_DIR .. "/Core" }

	filter {}
end

function m.link()
	m.include()
	links { "SameBoy" }
end

function m.project()
	project "pb12"
        kind "ConsoleApp"
        language "C"
        toolset "clang"

        files { SAMEBOY_DIR .. "/BootROMs/pb12.c" }

        configuration { "windows" }
            defines { "_CRT_SECURE_NO_WARNINGS" }
            includedirs { SAMEBOY_DIR .. "/Windows" }

        configuration { "macosx" }
            --[[xcodebuildsettings {
                ["MACOSX_DEPLOYMENT_TARGET"] = "10.14"
            }]]

            systemversion "10.14"
--[[
	project "SameBoyBootRoms"
		kind "Utility"
		dependson { "pb12", "bin2h" }
	
		files { SAMEBOY_DIR .. "/BootROMs/**.asm" }
	
		prebuildcommands {
			'rgbgfx -Z -u -c embedded -o "%{cfg.objdir}/SameBoyLogo.2bpp" "' .. BOOTROM_DIR .. '/SameBoyLogo.png"',
			'"%{cfg.buildtarget.directory}/pb12" < "%{cfg.objdir}/SameBoyLogo.2bpp" > "%{cfg.objdir}/SameBoyLogo.pb12"'
		}
	
		
		filter ("files:**.asm")
			buildmessage '%{file.basename}.asm'
	
			buildcommands {
				'rgbasm -i "' .. BOOTROM_RES_DIR .. '" -i "' .. BOOTROM_DIR .. '" -o "' .. BOOTROM_OBJ .. '" "%{file.relpath}"',
				'rgblink -o "' .. BOOTROM_BIN .. '" "' .. BOOTROM_OBJ .. '"',
				'"%{cfg.buildtarget.directory}/bin2h" "' .. BOOTROM_BIN .. '" "' .. BOOTROM_HEADER .. '" -id=%{file.basename}'
			}
	
			buildoutputs { BOOTROM_OBJ, BOOTROM_BIN, BOOTROM_HEADER }
]]

	project "SameBoy"
		kind "StaticLib"
		language "C"

		defines { "GB_INTERNAL", "GB_DISABLE_TIMEKEEPING", "GB_DISABLE_DEBUGGER", [[GB_VERSION="]] .. getVersion() .. [["]]  }

		m.include()

		externalincludedirs {
			"thirdparty",
			"thirdparty/spdlog/include"
		}

		includedirs {
			"src",
			"resources"
		}

		files {
			SAMEBOY_DIR .. "/Core/**.h",
			SAMEBOY_DIR .. "/Core/**.c",
		}
		excludes {
			SAMEBOY_DIR .. "/Core/sm83_disassembler.c",
			SAMEBOY_DIR .. "/Core/symbol_hash.c",
			SAMEBOY_DIR .. "/Core/debugger.c"
		}

		filter { "system:windows" }
			toolset "clang"
			includedirs { SAMEBOY_DIR .. "/Windows" }
			buildoptions {
				"-Wno-unused-variable",
				"-Wno-unused-function",
				"-Wno-missing-braces",
				"-Wno-switch",
				"-Wno-int-in-bool-context"
			}

		filter { "system:linux" }
			disablewarnings { "unused-variable" }
			buildoptions { "-Wno-implicit-function-declaration" }

		filter {}
end

return m
