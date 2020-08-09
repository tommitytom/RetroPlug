local dialog = require("dialog")
local menuutil = require("util.menu")
local pathutil = require("pathutil")
local filters = require("filters")

local NO_ACTIVE_SYSTEM = 0
local MAX_SYSTEMS = 4

local function loadProjectOrRom(project)
	return menuutil.loadHandler({ filters.PROJECT_FILTER, filters.ROM_FILTER, filters.ZIPPED_ROM_FILTER }, "project", function(path)
		local ext = pathutil.ext(path)
		if ext == "rplg" or ext == "retroplug" then
			return Project.load(path)
		elseif ext == "gb" or ext == "gbc" or ext == "zip" then
			Project.clear()
			return Project.loadRom(path)
		end
	end)
end

local function loadRom(project, idx, model)
	return menuutil.loadHandler({ filters.ROM_FILTER, filters.ZIPPED_ROM_FILTER }, "ROM", function(path)
		return Project.loadRom(path, idx, model)
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

local function saveProject(project, forceDialog)
	forceDialog = forceDialog or project.path == ""
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

local function projectMenu(menu, project)
	local settings = project.settings
	menu:action("New", function() Project.clear() end)
		:action("Load...", loadProjectOrRom(project))
		:action("Save", saveProject(project, false))
		:action("Save As...", saveProject(project, true))
		:separator()
		:subMenu("Save Options")
			:multiSelect({ "Prefer SRAM", "Prefer State" }, settings.saveType, function(v) settings.saveType = v end)
			:separator()
			:select("Include ROM", settings.packageRom, function(v) settings.packageRom = v end)
			:parent()
		:separator()
		:subMenu("Add System", #project.systems < MAX_SYSTEMS)
			:action("Load ROM...", loadRom(project, NO_ACTIVE_SYSTEM, GameboyModel.Auto))
			:action("Duplicate Selected", function()
				Project.duplicateSystem(Project.getSelectedIndex())
			end)
			:parent()
		:action("Remove System", function() Project.removeSystem(Project.getSelectedIndex()) end, #project.systems > 1)
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
		:action("Load State...", loadState(system, true))
		:action("Save State", saveState(system, false))
		:action("Save State As...", saveState(system, true))
		--[[:separator()
		:subMenu("UI Components")
			:parent()
		:subMenu("Audio Components")]]
end

local fs = require("fs")

local function findMissingRom(project, romPath)
	dialog.loadFile({ filters.ROM_FILTER, filters.ZIPPED_ROM_FILTER }, function(path)
		if path then
			for _, system in ipairs(project.systems) do
				if romPath == system.desc.romPath then
					system.desc.romPath = path
					local romData = fs.load(path)
					if romData then system:setRom(romData, true) end
				end
			end
		end
	end)
end

local function generateMainMenu(menu, project)
	local selected = Project.getSelected()

	if selected.desc.state == SystemState.Initialized or selected.desc.state == SystemState.Running then
		menu:title(selected.desc.romName):separator()
	end

	projectMenu(menu:subMenu("Project"), project)

	if selected.desc.state == SystemState.Running then
		systemMenu(menu:subMenu("System"), selected, project)
	elseif selected.desc.state == SystemState.RomMissing then
		menu:action("Find Missing ROM...", function() findMissingRom(project, selected.desc.romName) end)
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

local function generateStartMenu(menu, project)
	menu:action("Load Project or ROM...", loadProjectOrRom(project))
		:subMenu("Load ROM As...")
			:action("AGB...", loadRom(project, 1, GameboyModel.Agb))
			:action("CGB C...", loadRom(project, 1, GameboyModel.CgbC))
			:action("CGB E (default)...", loadRom(project, 1, GameboyModel.CgbE))
			:action("DMG B...", loadRom(project, 1, GameboyModel.DmgB))
end

local function generateMenu(menu, project)
	if Project.getSelectedIndex() ~= 0 then
		generateMainMenu(menu, project)
	else
		generateStartMenu(menu, project)
	end
end

return {
	generateMenu = generateMenu,
	loadProjectOrRom = loadProjectOrRom
}
