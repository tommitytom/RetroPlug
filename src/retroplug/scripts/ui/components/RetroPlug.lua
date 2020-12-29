local pathutil = require("pathutil")
local menuutil = require("util.menu")
local inpututil = require("util.input")
local util = require("util")
local filters = require("filters")
local Globals = require("Globals")

local _keysPressed = {}
local _buttonsPressed = {}

local RetroPlug = component({
	name = "RetroPlug",
	version = "1.0.0",
})

function RetroPlug.actions.nextSystem(down)
	if down == true then Project.nextSystem() end
end

function RetroPlug.actions.saveProject(down)
	if down == true then
		local forceDialog = Project.path == ""
		menuutil.saveHandler({ filters.PROJECT_FILTER }, "project", forceDialog, function(path)
			if path == nil or path == "" then
				if Project.path ~= nil and Project.path ~= "" then
					path = Project.path
				else
					log.error("Failed to save project: Invalid path")
					return
				end
			end

			log.info("Saving project to " .. path)
			return Project.save(path, true)
		end)()
	end
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
	if down == true then
		table.insert(pressed, key)
	else
		util.tableRemoveElement(pressed, key)
	end

	local handled = inpututil.handleInput(map.global, key, down, pressed, Project.buttonHooks)

	if handled ~= true and System ~= nil then
		handled = inpututil.handleInput(map.system, key, down, pressed, Project.buttonHooks, System)
	end

	return handled
end

function RetroPlug.onKey(key, down)
	if System and System.inputMap.key then
		return processInput(key, down, System.inputMap.key, _keysPressed)
	end

	return false
end

function RetroPlug.onPadButton(button, down)
	if System and System.inputMap.pad then
		return processInput(button, down, System.inputMap.pad, _buttonsPressed)
	end

	return false
end

return RetroPlug
