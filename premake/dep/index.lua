local m = {
	--SameBoy = dofile("SameBoy.lua"),
	glfw = 	dofile("glfw.lua"),
	bgfx = dofile("bgfx.lua"),
	liblsdj = dofile("liblsdj.lua"),
	lua = dofile("lua.lua"),
	r8brain = dofile("r8brain.lua"),
	zlib = dofile("zlib.lua"),
	minizip = dofile("minizip.lua"),
	box2d = dofile("box2d.lua"),
	freetype = dofile("freetype.lua"),
	simplefilewatcher = dofile("simplefilewatcher.lua"),
}

function m.allProjects()
	--m.SameBoy.project()
	m.bgfx.bxProject()
	m.bgfx.bimgProject()
	m.bgfx.bgfxProject()
	m.liblsdj.project()
	m.lua.project()
	m.r8brain.project()
	m.zlib.project()
	m.minizip.project()
	m.box2d.project()
	m.freetype.project()
	m.simplefilewatcher.project()

	if _OPTIONS["emscripten"] == nil then
		m.glfw.project()
	end
end

return m
