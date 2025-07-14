local m = {
	glfw = 	dofile("glfw.lua"),
	lua = dofile("lua.lua"),
	zlib = dofile("zlib.lua"),
	freetype = dofile("freetype.lua"),
	freetypeGl = dofile("freetype-gl.lua"),
	simplefilewatcher = dofile("simplefilewatcher.lua"),
	iplug2 = dofile("iplug2.lua"),
	glad = dofile("glad.lua"),
	bin2h = dofile("bin2h.lua"),
	yoga = dofile("yoga.lua"),
	stb = dofile("stb.lua"),
	gainput = dofile("gainput.lua"),
	pugl = dofile("pugl.lua"),
}

function m.allProjects()
	m.lua.project()
	m.zlib.project()
	m.freetype.project()
	m.freetypeGl.project()
	m.simplefilewatcher.project()
	m.iplug2.project()
	m.glad.project()
	m.bin2h.project()
	m.yoga.project()
	m.stb.project()
	m.gainput.project()

	if _OPTIONS["emscripten"] == nil then
		m.glfw.project()
		m.pugl.project()
	end
end

return m
