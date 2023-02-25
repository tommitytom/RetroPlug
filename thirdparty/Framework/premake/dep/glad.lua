local paths = dofile("../paths.lua")

local GLAD_DIR = paths.DEP_ROOT .. "glad"

local m = {}

function m.include()
	includedirs { GLAD_DIR .. "/include" }
end

function m.source()
	m.include()

	filter { "system:windows or linux" }
		files {
			GLAD_DIR .. "/src/gl.c",
			GLAD_DIR .. "/src/vulkan.c",
		}

	filter { "platforms:Emscripten" }
		files {
			GLAD_DIR .. "/src/gles2.c"
		}

	filter{}
end

function m.link()
	m.include()
	links { "glad" }
end

function m.project()
	project "glad"
		kind "StaticLib"

		m.source()
end

return m
