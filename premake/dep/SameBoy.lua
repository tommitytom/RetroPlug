local m = {}

local SAMEBOY_DIR = "thirdparty/SameBoy"

function m.include()
	defines { "GB_INTERNAL", "GB_DISABLE_TIMEKEEPING", "GB_DISABLE_DEBUGGER"  }
	sysincludedirs { SAMEBOY_DIR .. "/Core" }

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

		m.include()

		sysincludedirs {
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
			"src/sameboy/**.h",
			"src/sameboy/**.hpp",
			"src/sameboy/**.cpp",
			"src/sameboy/**.c",
		}
		excludes {
			SAMEBOY_DIR .. "/Core/sm83_disassembler.c",
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
			buildoptions { "-Wno-implicit-function-declaration" }

		filter {}
end

return m
