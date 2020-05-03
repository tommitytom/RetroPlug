local dialog = require("dialog")
local menuutil = require("util.menu")

local NO_ACTIVE_INSTANCE = -1
local MAX_INSTANCES = 4

local PROJECT_FILTER = { "RetroPlug Project", "*.retroplug" }
local ROM_FILTER = { "GameBoy ROM Files", "*.gb" }
local SAV_FILTER = { "GameBoy SAV Files", "*.sav" }

local function loadProject(project)
	return menuutil.loadHandler({ ROM_FILTER }, "project", function(path)
		return project:load(path)
	end)
end

local function loadRom(project, idx, model)
	return menuutil.loadHandler({ ROM_FILTER }, "ROM", function(path)
		return project:loadRom(idx, path, model)
	end)
end

local function loadSram(system)
	return menuutil.loadHandler({ SAV_FILTER }, "SAV", function(path)
		return system:loadSram(path)
	end)
end

local function saveProject(project, forceDialog)
	forceDialog = forceDialog or project.path == ""
	return menuutil.saveHandler({ PROJECT_FILTER }, "project", forceDialog, function(path)
		return project:save(path)
	end)
end

local function saveSram(system, forceDialog)
	return menuutil.saveHandler({ SAV_FILTER }, "SAV", forceDialog, function(path)
		return system:saveSram(path)
	end)
end

local function projectMenu(menu, project)
	local settings = project.settings
	menu:action("New", function() project:clear() end)
		:action("Load...", function() loadProject(project) end)
		:action("Save", function() saveProject(project, false) end)
		:action("Save As...", function() saveProject(project, true) end)
		:separator()
		:subMenu("Save Options")
			:multiSelect({ "Save SRAM", "Save State" }, settings.saveType, function(v) settings.saveType = v end)
			:parent()
		:separator()
		:subMenu("Add Instance", #project.instances < MAX_INSTANCES)
			:action("Load ROM...", function() loadRom(project, NO_ACTIVE_INSTANCE, GameboyModel.Auto) end)
			:action("Duplicate Selected", function() project:duplicateSelected() end)
			:parent()
		:action("Remove Instance", function() project:removeSelect() end, #project.instances > 1)
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
	menu:action("Load ROM...", function() loadRom(project, system.index) end)
		:subMenu("Load ROM As")
			:action("AGB...", function() loadRom(project, system.index, GameboyModel.Agb) end)
			:action("CGB C...", function() loadRom(project, system.index, GameboyModel.CgbC) end)
			:action("CGB E (default)...", function() loadRom(project, system.index, GameboyModel.CgbE) end)
			:action("DMG B...", function() loadRom(project, system.index, GameboyModel.DmgB) end)
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
		:action("Load .sav...", function() loadSram(system) end)
		:action("Save .sav", function() saveSram(system, false) end)
		:action("Save .sav As...", function() saveSram(system, true) end)
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
	local selected = project.selected
	menu:title(selected.romName):separator()
	projectMenu(menu:subMenu("Project"), project)

	if selected.state == EmulatorInstanceState.Running then
		systemMenu(menu:subMenu("System"), selected)
	elseif selected.state == EmulatorInstanceState.RomMissing then
		menu:action("Find Missing ROM...", function() findMissingRom(selected) end)
	end

	menu:subMenu("Settings")
			:action("Open Settings Folder...", function()  end)
			:parent()
		:separator()
		:select("Game Link", selected.sameBoySettings.gameLink, function(v) selected.sameBoySettings.gameLink = v end)
end

local function generateMenu(menu, project)
	if project.selected ~= nil then
		generateMainMenu(menu, project)
	end
end

return {
	generateMenu = generateMenu
}
