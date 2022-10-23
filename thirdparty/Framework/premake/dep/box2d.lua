local BOX2D_DIR = _ROOT_PATH .. "thirdparty/box2d"

local m = {}

function m.include()
	sysincludedirs { BOX2D_DIR .. "/include" }
end

function m.source()
	m.include()

	files {
		BOX2D_DIR .. "/src/**.cpp"
	}
end

function m.link()
	m.include()
	links { "box2d" }
end

function m.project()
	project "box2d"
		kind "StaticLib"

		m.source()

		--filter "system:windows"
			--disablewarnings { "4334", "4098", "4244" }
end

return m
