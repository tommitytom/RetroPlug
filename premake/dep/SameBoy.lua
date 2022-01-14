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
		toolset "clang"
		--flags { "LinkTimeOptimization" }

		m.include()

		includedirs {
			"src",
			"thirdparty",
			"thirdparty/spdlog/include"
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

		disablewarnings { "int-in-bool-context" }

		filter { "system:windows" }
			includedirs { SAMEBOY_DIR .. "/Windows" }
end

return m
