local m = {
	glfw = 	dofile("glfw.lua"),
	SameBoy = dofile("SameBoy.lua"),
	bgfx = dofile("bgfx.lua"),
	liblsdj = dofile("liblsdj.lua"),
	lua = dofile("lua.lua"),
	zlib = dofile("zlib.lua"),
}

function m.allProjects()
	m.SameBoy.project()
	m.bgfx.bxProject()
	m.bgfx.bimgProject()
	m.bgfx.bgfxProject()
	m.liblsdj.project()
	m.lua.project()
	m.zlib.project()

	if _OPTIONS["emscripten"] == nil then
		m.glfw.project()
	end
end

return m