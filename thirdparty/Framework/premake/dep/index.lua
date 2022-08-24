local m = {
	glfw = 	dofile("glfw.lua"),
	bgfx = dofile("bgfx.lua"),
	lua = dofile("lua.lua"),
	zlib = dofile("zlib.lua"),
	box2d = dofile("box2d.lua"),
	freetype = dofile("freetype.lua"),
	simplefilewatcher = dofile("simplefilewatcher.lua"),
}

function m.allProjects()
	--m.SameBoy.project()
	m.bgfx.bxProject()
	m.bgfx.bimgProject()
	m.bgfx.bgfxProject()
	m.lua.project()
	m.zlib.project()
	m.box2d.project()
	m.freetype.project()
	m.simplefilewatcher.project()

	if _OPTIONS["emscripten"] == nil then
		m.glfw.project()
	end
end

return m
