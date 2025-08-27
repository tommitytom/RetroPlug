local paths = dofile("../paths.lua")
local GAINPUT_DIR = paths.DEP_ROOT .. "gainput"
local m = {}

function m.include()
	includedirs { GAINPUT_DIR .. "/lib/include" }
end

function m.source()
	m.include()

    files {
		GAINPUT_DIR .. "/lib/include/gainput/**.h",
		GAINPUT_DIR .. "/lib/source/gainput/**.cpp",
		GAINPUT_DIR .. "/lib/source/gainput/**.mm"
	}

	filter { "system:windows" }
		disablewarnings { "4267", "4244" }

	filter{}
end

function m.link()
	m.include()
	links { "gainput" }

    filter { "system:windows" }
		links { "xinput" }

    filter {}
end

function m.project()
	project "gainput"
		kind "StaticLib"
		m.source()
end

return m
