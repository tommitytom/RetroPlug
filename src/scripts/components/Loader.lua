local pathutil = require("pathutil")

local Loader = component({ name = "Loader", version = "1.0.0", global = true })

function Loader:onDrop(paths)
	local projects = {}
	local roms = {}
	local savs = {}

	for _, v in ipairs(paths) do
		local ext = pathutil.ext(v)
		if ext == "retroplug" then table.insert(projects, v) end
		if ext == "gb" then table.insert(roms, v) end
		if ext == "sav" then table.insert(savs, v) end
	end

	if #projects > 0 then
		_loadProject(projects[1])
	elseif #roms > 0 then
		local idx = -1
		if Active ~= nil then
			idx = Active.desc.idx
		end

		_loadRomAtPath(idx, roms[1])
		--Active.system:loadRom(roms[1])
	elseif #savs > 0 then
		--Active.system:loadSav(savs[1])
	end
end

return Loader
