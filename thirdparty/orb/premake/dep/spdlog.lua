local paths = dofile("../paths.lua")

local SPDLOG_DIR = paths.DEP_ROOT .. "spdlog"

local m = {}

function m.include()
	filter{}
	defines { "SPDLOG_COMPILED_LIB", "SPDLOG_USE_STD_FORMAT", "FMT_UNICODE=1" }
	includedirs {
		SPDLOG_DIR .. "/include",
	}
end

function m.source()
	filter{}
	m.include()

	files {
		SPDLOG_DIR .. "/include/**.h",
		SPDLOG_DIR .. "/src/async.cpp",
		SPDLOG_DIR .. "/src/cfg.cpp",
		SPDLOG_DIR .. "/src/color_sinks.cpp",
		SPDLOG_DIR .. "/src/file_sinks.cpp",
		SPDLOG_DIR .. "/src/spdlog.cpp",
		SPDLOG_DIR .. "/src/stdout_sinks.cpp",
	}
end

function m.link()
	filter{}
	m.include()
	links { "spdlog" }
end

function m.project()
	project "spdlog"
		kind "StaticLib"
		m.source()
end

return m
