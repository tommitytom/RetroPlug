local pathutil = require("pathutil")
local menuutil = require("util.menu")
local inpututil = require("util.input")
local filters = require("filters")

local RetroPlug = component({
	name = "RetroPlug",
	version = "1.0.0",
})

local class = require("class")
local RetroPlugActions = class()
function RetroPlugActions:init(project)
	self.project = project
end

function RetroPlugActions:nextSystem(down)
	if down == true then self.project:nextSystem() end
end

function RetroPlugActions:saveProject(down)
	print("SAVEEEEE")
	if down == true then
		return menuutil.saveHandler({ filters.PROJECT_FILTER }, "project", false, function(path)
			return self.project:save(path, true)
		end)
	end
end

function RetroPlug:init()
	self:registerActions(RetroPlugActions(self:project()))
end

function RetroPlug:onDrop(paths)
	local proj = self:project()
	local projects = {}
	local roms = {}
	local savs = {}

	for _, v in ipairs(paths) do
		local ext = pathutil.ext(v)
		if ext == "retroplug" or ext == "rplg" then table.insert(projects, v) end
		if ext == "gb" then table.insert(roms, v) end
		if ext == "sav" then table.insert(savs, v) end
	end

	local selected = proj:getSelectedIndex()
	if #projects > 0 then
		proj:load(projects[1])
		return true
	elseif #roms > 0 then
		if #proj.systems == 1 then proj:clear() end
		proj:loadRom(roms[1], selected)
		return true
	elseif #savs > 0 then
		local system = proj:getSelected()
		if system ~= nil then system:loadSram(savs[1], true) end
		return true
	end

	return false
end

function RetroPlug:onKey(key, down)
	local bm = self:project().buttonMap
	local system = self:project().selectedSystem

	assert(bm)
	assert(system)
	assert(self._keysPressed)

	local handled = inpututil.handleInput(bm.keys.global, key, down, self._keysPressed)
	if handled ~= true and system ~= nil then
		assert(system.keysPressed)
		handled = inpututil.handleInput(bm.keys.system, key, down, system.keysPressed, system:buttons())
	end

	return handled
end

function RetroPlug:onPadButton(button, down)
	return inpututil.handleInput(self._padMap, button, down, self._padbuttonsPressed, self._buttonHooks, self:system():buttons())
end

return RetroPlug
