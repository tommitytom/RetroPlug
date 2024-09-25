local m = {}

local SAMEBOY_DIR = "thirdparty/SameBoy"

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
