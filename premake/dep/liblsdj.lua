local LIBLSDJ_DIR = "thirdparty/liblsdj"

local m = {}

function m.include()
	sysincludedirs { LIBLSDJ_DIR .. "/liblsdj/include/lsdj" }
end

function m.source()
	m.include()

	files {
		LIBLSDJ_DIR .. "/liblsdj/include/lsdj/**.h",
		LIBLSDJ_DIR .. "/liblsdj/src/**.c"
	}
end

function m.link()
	m.include()
	links { "liblsdj" }
end

function m.project()
	project "liblsdj"
		kind "StaticLib"

		m.source()

		filter "system:windows"
			disablewarnings { "4334", "4098", "4244" }
end

return m