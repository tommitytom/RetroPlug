local paths = dofile("../paths.lua")

local SDL_DIR = paths.DEP_ROOT .. "SDL"

local m = {}

function m.include()
	includedirs { SDL_DIR .. "/include" }
end

function m.link()
	m.include()
    libdirs { SDL_DIR .. "/lib/x64" }
	links { "SDL2", "GLESv2", "mali" }
end

return m
