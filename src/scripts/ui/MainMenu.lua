local dialog = require("dialog")
local menuutil = require("util.menu")
local pathutil = require("pathutil")

local NO_ACTIVE_INSTANCE = -1
local MAX_INSTANCES = 4

local PROJECT_FILTER = { "RetroPlug Project", "*.retroplug" }
local ROM_FILTER = { "GameBoy ROM Files", "*.gb" }
local SAV_FILTER = { "GameBoy SAV Files", "*.sav" }

local function loadProjectOrRom(project)
	return menuutil.loadHandler({ PROJECT_FILTER, ROM_FILTER }, "project", function(path)
		local ext = pathutil.ext(path)
		if ext == "retroplug" then
			return project:load(path)
		elseif ext == "gb" then
			project:clear()
			return project:loadRom(path)
		end
	end)
end

local function loadRom(project, idx, model)
	return menuutil.loadHandler({ ROM_FILTER }, "ROM", function(path)
		return project:loadRom(path, idx, model)
	end)
end

local function loadSram(system, reset)
	return menuutil.loadHandler({ SAV_FILTER }, "SAV", function(path)
		return system:loadSram(path, reset)
	end)
end

local function saveProject(project, forceDialog)
	forceDialog = forceDialog or project.path == ""
	return menuutil.saveHandler({ PROJECT_FILTER }, "project", forceDialog, function(path)
		return project:save(path, true)
	end)
end

local function saveSram(system, forceDialog)
	return menuutil.saveHandler({ SAV_FILTER }, "SAV", forceDialog, function(path)
		return system:saveSram(path)
	end)
end

local function projectMenu(menu, project)
	local settings = project._native.settings
	menu:action("New", function() project:clear() end)
		:action("Load...", loadProjectOrRom(project))
		:action("Save", saveProject(project, false))
		:action("Save As...", saveProject(project, true))
		:separator()
		:subMenu("Save Options")
			:multiSelect({ "Save SRAM", "Save State" }, settings.saveType, function(v) settings.saveType = v end)
			:parent()
		:separator()
		:subMenu("Add Instance", #project.systems < MAX_INSTANCES)
			:action("Load ROM...", loadRom(project, NO_ACTIVE_INSTANCE, GameboyModel.Auto))
			:action("Duplicate Selected", function() project:duplicateSystem(project:getSelectedIndex()) end)
			:parent()
		:action("Remove Instance", function() project:removeSystem(project:getSelectedIndex()) end, #project.systems > 1)
		:subMenu("Layout")
			:multiSelect({ "Auto", "Row", "Column", "Grid" }, settings.layout, function(v) settings.layout = v end)
			:parent()
		:subMenu("Zoom")
			:multiSelect({ "1x", "2x", "3x", "4x" }, settings.zoom - 1, function(v) settings.zoom = v + 1 end)
			:parent()
		:separator()
		:subMenu("Audio Routing")
			:multiSelect({
				"Stereo Mixdown",
				"Two Channels Per Instance"
			}, settings.audioRouting, function(v) settings.audioRouting = v end)
			:parent()
		:subMenu("MIDI Routing")
			:multiSelect({
				"All Channels to All Instances",
				"Four Channels Per Instance",
				"One Channel Per Instance",
			}, settings.midiRouting, function(v) settings.midiRouting = v end)
end

local function systemMenu(menu, system, project)
	menu:action("Load ROM...", loadRom(project, system.desc.idx + 1))
		:subMenu("Load ROM As")
			:action("AGB...", loadRom(project, system.desc.idx + 1, GameboyModel.Agb))
			:action("CGB C...", loadRom(project, system.desc.idx + 1, GameboyModel.CgbC))
			:action("CGB E (default)...", loadRom(project, system.desc.idx + 1, GameboyModel.CgbE))
			:action("DMG B...", loadRom(project, system.desc.idx + 1, GameboyModel.DmgB))
			:parent()
		:action("Reset", function() system:reset() end)
		:subMenu("Reset As")
			:action("AGB", function() system:reset(GameboyModel.Agb) end)
			:action("CGB C", function() system:reset(GameboyModel.CgbC) end)
			:action("CGB E (default)", function() system:reset(GameboyModel.CgbE) end)
			:action("DMG B", function() system:reset(GameboyModel.DmgB) end)
			:parent()
		:separator()
		:action("New .sav", function() system:clearSram(true) end)
		:action("Load .sav...", loadSram(system, true))
		:action("Save .sav", saveSram(system, false))
		:action("Save .sav As...", saveSram(system, true))
		:separator()
		:subMenu("UI Components")
			:parent()
		:subMenu("Audio Components")
end

local function findMissingRom(system)
	dialog.loadFile({ ROM_FILTER }, function(path)

	end)
end

local function generateMainMenu(menu, project)
	local selected = project:getSelected()
	menu:title(selected.desc.romName):separator()
	projectMenu(menu:subMenu("Project"), project)

	if selected.desc.state == SystemState.Running then
		systemMenu(menu:subMenu("System"), selected, project)
	elseif selected.desc.state == SystemState.RomMissing then
		menu:action("Find Missing ROM...", function() findMissingRom(selected) end)
	end

	local sameBoySettings = selected.desc.sameBoySettings
	menu:subMenu("Settings")
			:action("Open Settings Folder...", function()  end)
			:parent()
		:separator()
		:select("Game Link", sameBoySettings.gameLink, function(v) sameBoySettings.gameLink = v end)
end

local function generateStartMenu(menu, project)
	menu:action("Load Project or ROM...", loadProjectOrRom(project))
		:subMenu("Load ROM As...")
			:action("AGB...", loadRom(project, 1, GameboyModel.Agb))
			:action("CGB C...", loadRom(project, 1, GameboyModel.CgbC))
			:action("CGB E (default)...", loadRom(project, 1, GameboyModel.CgbE))
			:action("DMG B...", loadRom(project, 1, GameboyModel.DmgB))
end

local function generateMenu(menu, project)
	if project:getSelectedIndex() ~= 0 then
		generateMainMenu(menu, project)
	else
		generateStartMenu(menu, project)
	end
end

return {
	generateMenu = generateMenu,
	loadProjectOrRom = loadProjectOrRom
}
