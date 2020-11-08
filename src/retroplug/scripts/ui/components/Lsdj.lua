local dialog = require("dialog")
local KeyboardActions = require("components.lsdj.actions")
local lsdj = require("liblsdj.liblsdj")
local fs = require("fs")
local pathutil = require("pathutil")

local ROM_FILTER = { "LSDj Rom Files", { "*.gb", "*.zip" } }
local ROM_FILTER_NO_ZIP = { "LSDj Rom Files", { "*.gb" } }
local SAV_FILTER = { "LSDj Sav Files", "*.sav" }
local SONG_FILTER = { "LSDj Song Files", "*.lsdsng" }
local KIT_FILTER = { "LSDj Kit Files", "*.kit" }

local function formatName(idx, name, empty)
	if empty == true then name = "(Empty)" end
	return string.format("%02X", idx) .. ": " .. name
end

local Lsdj = component({
	name = "LSDj",
	version = "1.0.0",
	requirements = { romName = "LSDj*" },
	systemState = {
		littleFm = false,
		overclock = false,
		keyboardShortcuts = true
	}
})

Lsdj.actions = KeyboardActions

function Lsdj.init()
	--Project.state.lsdj
end

local function isLsdj(system)
	return system.desc.romName:match("LSDj*") ~= nil
end

function Lsdj.requires()
	local system = Project.getSelected()
	return system ~= nil and isLsdj(system)
end

function Lsdj.onDeserialize(states)
	--[[for i, v in ipairs(states) do
		self.systems[i]:setComponentState("LSDj", v)
	end]]
end

function Lsdj.onDeserialized()

end

function Lsdj.onBeforeButton(button, down)
	local system = Project.getSelected()
	if isLsdj(system) then
		KeyboardActions._handleButtonPress(button, down, system)
	end

	return false
end

function Lsdj.onPatchRom(romData)

end

function Lsdj.onBeforeRomLoad(romData)
	-- Patch the rom
	--liblsdj.parseRom(romData)
end

function Lsdj.onBeforeSavLoad(savData)
	-- Patch the sav
	--liblsdj.parseSav(savData)
end

function Lsdj.onRomLoad(romData)

end

local function overclockPatch(data, overclock)
	local overClockIdx = data:findSequence({ 0x3e }, 1, { 0xe0, 0x07 })
	if overClockIdx ~= -1 then
		local v = 0x04
		if overclock == true then v = 0x07 end
		data:write(overClockIdx + 1, { v })
	end
end

function Lsdj.updateRom()
	--local d = self:system().romData
	--overclockPatch(d, self._state.overclock)
end

local function upgradeRom(path, system, rom)
	local romData = fs.load(path)
	if romData ~= nil then
		local newRom = lsdj.loadRom(romData)
		if newRom ~= nil then
			newRom:copyFrom(rom)
			system:setRom(newRom:toBuffer(), true)
		end
	end
end

function Lsdj.onDrop(paths)
	local system = Project.getSelected()

	local songs = {}
	local kits = {}
	for _, v in ipairs(paths) do
		local ext = pathutil.ext(v)
		if ext == "lsdsng" then table.insert(songs, v) end
		if ext == "kit" then table.insert(kits, v) end
	end

	if #kits > 0 then
		local rom = lsdj.loadRom(system:rom())
		local err = rom:importKits(kits)

		if err == nil then
			system:setRom(rom:toBuffer(), true)
		else
			print("Importing kits failed:")
			table.foreach(err, print)
		end
	end

	if #songs > 0 then
		local sav = lsdj.loadSav(system:sram())
		local err = sav:importSongs(songs)

		if err == nil then
			system:setSram(sav:toBuffer(), true)
		else
			print("Importing songs failed:")
			table.foreach(err, print)
		end
	end
end

local function importSongFromFile(sav, songIdx, system, reload)
	dialog.loadFile({ SONG_FILTER }, function(path)
		local data = fs.load(path)
		if data ~= nil then
			sav:importSong(songIdx, data)
			system:setSram(sav:toBuffer(), reload)
		end
	end)
end

local function createSongsMenu(system, menu, sav)
	menu:action("Import (and reset)...", function()
		dialog.loadFiles({ SONG_FILTER, SAV_FILTER }, function(paths)
			local err = sav:importSongs(paths)
			if err == nil then
				sav:toBuffer(system:sram())
				system:setSram(system:sram(), true)
			else
				print("Import failed:")
				table.foreach(err, print)
			end
		end)
	end)
	:action("Export All...", function()
		dialog.selectDirectory(function(path)
			local err = sav:exportSongs(path)
			if err ~= nil then
				print("Export failed:")
				table.foreach(err, print)
			end
		end)
	end)
	:separator()

	local songs = sav:getSongs()
	for _, song in ipairs(songs) do
		local songName = formatName(song.idx, song.name, song.empty)
		local songMenu = menu:subMenu(songName)

		if song.empty == false then
			songMenu:action("Load (and reset)", function()
				sav:loadSong(song.idx)
				system:setSram(sav:toBuffer(), true)
			end)
			:action("Export .lsdsng...", function()
				dialog.saveFile({ SONG_FILTER }, song.name, function(path)
					sav:exportSong(song.idx, path)
				end)
			end)
			:action("Replace...", function() importSongFromFile(sav, song.idx, system, false) end)
			:action("Delete", function()
				sav:deleteSong(song.idx)
				system:setSram(sav:toBuffer(), true)
			end)
		else
			songMenu:action("Import...", function() importSongFromFile(sav, song.idx, system, false) end)
			songMenu:action("Import and Load...", function() importSongFromFile(sav, song.idx, system, true) end)
		end
	end
end

local function createKitsMenu(system, menu, rom)
	menu:action("Import (and reset)...", function()
		dialog.loadFiles({ KIT_FILTER, ROM_FILTER }, function(paths)
			local err = rom:importKits(paths)
			if err == nil then
				system:setRom(rom:toBuffer(), true)
			else
				-- Log error
			end
		end)
	end)
	:action("Export All...", function()
		dialog.selectDirectory(function(path)
			rom:exportKits(path)
		end)
	end)
	:separator()

	local kits = rom:getKits()
	for i, kit in ipairs(kits) do
		local kitName = formatName(i - 1, kit.name, kit.data == nil)
		local kitMenu = menu:subMenu(kitName)

		if kit.data ~= nil then
			kitMenu:action("Replace...", function()
				dialog.loadFile({ KIT_FILTER }, function(path)
					kit:init(path)
					system:setRom(rom:toBuffer())
				end)
			end)
			:action("Export...", function()
				dialog.saveFile({ KIT_FILTER }, kit.name .. ".kit", function(path)
					kit:toFile(path)
				end)
			end)
			:action("Delete", function()
				rom:eraseKit(i)
				system:setRom(rom:toBuffer())
			end)
		else
			kitMenu:action("Load (and reset)...", function()
				dialog.loadFile({ KIT_FILTER }, function(path)
					kit:init(path)
					system:setRom(rom:toBuffer(), true)
				end)
			end)
		end
	end
end

function Lsdj.onMenu(menu)
	local root = menu:subMenu("LSDj")
	local system = Project.getSelected()
	local rom = lsdj.loadRom(system:rom())
	local sav = lsdj.loadSav(system:sram())

	root:subMenu("Sync")
	root:separator()

	if sav ~= nil then createSongsMenu(system, root:subMenu("Songs"), sav) end
	createKitsMenu(system, root:subMenu("Kits"), rom)

	root:separator()
		--:select("Keyboard Shortcuts", self._state.keyboardShortcuts, function(v) self._state.keyboardShortcuts = v end)
		--:separator()
		--[[:subMenu("Patches")
			:select("LitteFM", self._state.littleFm, function(v) self._state.littleFm = v; self:updateRom() end)
			:select("4x Overclock", self._state.overclock, function(v) self._state.overclock = v; self:updateRom() end)
			:parent()]]
		:action("Export ROM...", function()
			dialog.saveFile({ ROM_FILTER_NO_ZIP }, system.desc.romName, function(path) rom:toFile(path) end)
		end)
		:action("Upgrade To...", function()
			dialog.loadFile({ ROM_FILTER }, function(path) upgradeRom(path, system, rom) end)
		end)
end

return Lsdj
