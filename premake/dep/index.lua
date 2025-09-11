local m = {
	SameBoy = dofile("SameBoy.lua"),
	liblsdj = dofile("liblsdj.lua"),
	r8brain = dofile("r8brain.lua"),
	minizip = dofile("minizip.lua"),
	reflcpp = dofile("refl-cpp.lua"),
	enkits = dofile("enkits.lua"),
}

function m.allProjects()
	m.SameBoy.project()
	m.liblsdj.project()
	m.r8brain.project()
	m.minizip.project()
	m.reflcpp.project()
	m.enkits.project()
end

return m