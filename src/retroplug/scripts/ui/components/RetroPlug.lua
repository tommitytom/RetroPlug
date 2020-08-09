local pathutil = require("pathutil")
local menuutil = require("util.menu")
local inpututil = require("util.input")
local util = require("util")
local filters = require("filters")


local class = require("class")
local RetroPlugActions = class()
function RetroPlugActions:init(project)
	self.project = project
end

function RetroPlugActions:nextSystem(down)
	if down == true then self.project.nextSystem() end
end

function RetroPlugActions:saveProject(down)
	print("SAVEEEEE")
	if down == true then
		return menuutil.saveHandler({ filters.PROJECT_FILTER }, "project", false, function(path)
			return self.project.save(path, true)
		end)
	end
end

local _keysPressed = {}
local _buttonsPressed = {}

local RetroPlug = component({
	name = "RetroPlug",
	version = "1.0.0",
})


function RetroPlug.init()
	--self:registerActions(RetroPlugActions(Project))
end

function RetroPlug.actions.nextSystem(down)
	if down == true then Project.nextSystem() end
end

function RetroPlug.onDrop(paths)
	local projects = {}
	local roms = {}
	local savs = {}

	for _, v in ipairs(paths) do
		local ext = pathutil.ext(v)
		if ext == "retroplug" or ext == "rplg" then table.insert(projects, v) end
		if ext == "gb" then table.insert(roms, v) end
		if ext == "sav" then table.insert(savs, v) end
	end

	local selected = Project.getSelectedIndex()
	if #projects > 0 then
		Project.load(projects[1])
		return true
	elseif #roms > 0 then
		if #Project.systems == 1 then Project.clear() end
		Project.loadRom(roms[1], selected)
		return true
	elseif #savs > 0 then
		local system = Project.getSelected()
		if system ~= nil then system:loadSram(savs[1], true) end
		return true
	end

	return false
end

local function processInput(key, down, map, pressed)
	local system = Project.getSelected()
	local buttonHooks = Project.buttonHooks

	if down == true then
		table.insert(pressed, key)
	else
		util.tableRemoveElement(pressed, key)
	end

	local handled = inpututil.handleInput(map.global, key, down, pressed, buttonHooks)
	if handled ~= true and system ~= nil then
		handled = inpututil.handleInput(map.system, key, down, pressed, buttonHooks, system)
	end

	return handled
end

function RetroPlug.onKey(key, down)
	return processInput(key, down, Project.inputMap.key, _keysPressed)
end

function RetroPlug.onPadButton(button, down)
	return processInput(button, down, Project.inputMap.pad, _buttonsPressed)
end

return RetroPlug
