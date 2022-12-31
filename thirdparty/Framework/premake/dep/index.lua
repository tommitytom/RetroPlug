local m = {
	glfw = 	dofile("glfw.lua"),
	bgfx = dofile("bgfx.lua"),
	lua = dofile("lua.lua"),
	zlib = dofile("zlib.lua"),
	box2d = dofile("box2d.lua"),
	freetype = dofile("freetype.lua"),
	simplefilewatcher = dofile("simplefilewatcher.lua"),
	iplug2 = dofile("iplug2.lua"),
	glad = dofile("glad.lua"),
	bin2h = dofile("bin2h.lua"),
}

function m.allProjects()
	m.bgfx.bxProject()
	m.bgfx.bimgProject()
	m.bgfx.bgfxProject()
	m.lua.project()
	m.zlib.project()
	m.box2d.project()
	m.freetype.project()
	m.simplefilewatcher.project()
	m.iplug2.project()
	m.glad.project()
	m.bin2h.project()

	if _OPTIONS["emscripten"] == nil then
		m.glfw.project()
	end
end

return m
