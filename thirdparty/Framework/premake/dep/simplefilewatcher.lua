local SFW_DIR = "thirdparty/simplefilewatcher"

local m = {}

function m.include()
	sysincludedirs { SFW_DIR .. "/include" }
end

function m.source()
	m.include()

	files {
		SFW_DIR .. "/include/**.h",
		SFW_DIR .. "/source/**.cpp"
	}
end

function m.link()
	m.include()
	links { "simplefilewatcher" }
end

function m.project()
	project "simplefilewatcher"
		kind "StaticLib"

		m.source()

		--filter "system:windows"
			--disablewarnings { "4334", "4098", "4244" }
end

return m
