local dialog = require("dialog")
local menuutil = require("util.menu")
local pathutil = require("pathutil")
local filters = require("filters")
local fs = require("fs")

local NO_ACTIVE_SYSTEM = 0
local MAX_SYSTEMS = 4

local MainMenu = {}

function MainMenu.loadProjectOrRom()
	return menuutil.loadHandler({ filters.PROJECT_FILTER, filters.ROM_FILTER, filters.ZIPPED_ROM_FILTER }, "project", function(path)
		local ext = pathutil.ext(path)
		if ext == "rplg" or ext == "retroplug" then
			return Project.load(path)
		elseif ext == "gb" or ext == "gbc" or ext == "zip" then
			Project.clear()
			return Project.addSystem():loadRom(path)
		end
	end)
end

local function loadRom(idx, model)
	return menuutil.loadHandler({ filters.ROM_FILTER, filters.ZIPPED_ROM_FILTER }, "ROM", function(path)
		return Project.addSystem():loadRom(path, idx, model)
	end)
end

local function loadSram(system, reset)
	return menuutil.loadHandler({ filters.SAV_FILTER }, "SAV", function(path)
		return system:loadSram(path, reset)
	end)
end

local function loadState(system, reset)
	return menuutil.loadHandler({ filters.STATE_FILTER }, "state", function(path)
		return system:loadState(path, reset)
	end)
end

local function saveProject(forceDialog)
	forceDialog = forceDialog or Project.path == ""
	return menuutil.saveHandler({ filters.PROJECT_FILTER }, "project", forceDialog, function(path)
		return Project.save(path, true)
	end)
end

local function saveSram(system, forceDialog)
	return menuutil.saveHandler({ filters.SAV_FILTER }, "SAV", forceDialog, function(path)
		return system:saveSram(path)
	end)
end

local function saveState(system, forceDialog)
	return menuutil.saveHandler({ filters.STATE_FILTER }, "state", forceDialog, function(path)
		return system:saveState(path)
	end)
end

local function projectMenu(menu)
	local settings = Project.settings
	menu:action("New", function() Project.clear() end)
		:action("Load...", MainMenu.loadProjectOrRom())
		:action("Save", saveProject(false))
		:action("Save As...", saveProject(true))
		:separator()
		:subMenu("Save Options")
			:multiSelect({ "Prefer SRAM", "Prefer State" }, settings.saveType, function(v) settings.saveType = v end)
			:separator()
			:select("Include ROM", settings.packageRom, function(v) settings.packageRom = v end)
			:parent()
		:separator()
		:subMenu("Add System", #Project.systems < MAX_SYSTEMS)
			:action("Load ROM...", loadRom(NO_ACTIVE_SYSTEM, GameboyModel.Auto))
			:action("Duplicate Selected", function()
				Project.duplicateSystem(Project.getSelectedIndex())
			end)
			:parent()
		:action("Remove System", function() Project.removeSystem(Project.getSelectedIndex()) end, #Project.systems > 1)
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
				"Two Channels Per System"
			}, settings.audioRouting, function(v) settings.audioRouting = v end)
			:parent()
		:subMenu("MIDI Routing")
			:multiSelect({
				"All Channels to All Systems",
				"Four Channels Per System",
				"One Channel Per System",
			}, settings.midiRouting, function(v) settings.midiRouting = v end)
end

local function systemMenu(menu, system)
	menu:action("Load ROM...", loadRom(system.desc.idx + 1))
		:subMenu("Load ROM As")
			:action("AGB...", loadRom(system.desc.idx + 1, GameboyModel.Agb))
			:action("CGB C...", loadRom(system.desc.idx + 1, GameboyModel.CgbC))
			:action("CGB E (default)...", loadRom(system.desc.idx + 1, GameboyModel.CgbE))
			:action("DMG B...", loadRom(system.desc.idx + 1, GameboyModel.DmgB))
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
		:action("Load State...", loadState(system, true))
		:action("Save State", saveState(system, false))
		:action("Save State As...", saveState(system, true))
		--[[:separator()
		:subMenu("UI Components")
			:parent()
		:subMenu("Audio Components")]]
end

local function findMissingRom(romPath)
	dialog.loadFile({ filters.ROM_FILTER, filters.ZIPPED_ROM_FILTER }, function(path)
		if path then
			for _, system in ipairs(Project.systems) do
				if romPath == system.desc.romPath then
					system.desc.romPath = path
					local romData = fs.load(path)
					if romData then system:setRom(romData, true) end
				end
			end
		end
	end)
end

local function generateMainMenu(menu)
	local selected = Project.getSelected()

	if selected.desc.state == SystemState.Initialized or selected.desc.state == SystemState.Running then
		menu:title(selected.desc.romName):separator()
	end

	projectMenu(menu:subMenu("Project"))

	if selected.desc.state == SystemState.Running then
		systemMenu(menu:subMenu("System"), selected)
	elseif selected.desc.state == SystemState.RomMissing then
		menu:action("Find Missing ROM...", function() findMissingRom(selected.desc.romName) end)
	end

	local sameBoySettings = selected.desc.sameBoySettings
	menu:subMenu("Settings")
			:subMenu("Keyboard")
				:parent()
			:subMenu("Joypad")
				:action("Default")
				:parent()
			:subMenu("MIDI")
				:action("Default")
				:separator()
				:action("peterswimm")
				:parent()
			:separator()
			:action("Open Settings Folder...", function()
				nativeshell.openShellFolder(nativeshell.getConfigPath())
			end)
			:parent()
		:separator()
		:select("Game Link", sameBoySettings.gameLink, function(v)
			sameBoySettings.gameLink = v
			selected:updateSettings()
		end)
end

local function generateStartMenu(menu)
	menu:action("Load Project or ROM...", MainMenu.loadProjectOrRom())
		:subMenu("Load ROM As...")
			:action("AGB...", loadRom(1, GameboyModel.Agb))
			:action("CGB C...", loadRom(1, GameboyModel.CgbC))
			:action("CGB E (default)...", loadRom(1, GameboyModel.CgbE))
			:action("DMG B...", loadRom(1, GameboyModel.DmgB))
end

function MainMenu.generateMenu(menu)
	if Project.getSelectedIndex() ~= 0 then
		generateMainMenu(menu)
	else
		generateStartMenu(menu)
	end
end

return MainMenu
