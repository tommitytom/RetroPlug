local Lsdj = component({ name = "LSDj", romName = "LSDj*" })
local dialog = require("dialog")

local fs = require("fs")
local KeyboardActions = require("components.lsdj.actions")
local lsdj = require("liblsdj.liblsdj")

function Lsdj:init()
	self._valid = false
	self._kits = {}
	self._songs = {}
	self._littleFm = false
	self._overclock = false

	self._keyboardActions = KeyboardActions(self:system())
	self:registerActions(self._keyboardActions)
end

function Lsdj:onBeforeButton(button, down)
	self._keyboardActions:_handleButtonPress(button, down)
end

function Lsdj:onPatchRom(romData)

end

function Lsdj:onBeforeRomLoad(romData)
	-- Patch the rom
	--liblsdj.parseRom(romData)
end

function Lsdj:onBeforeSavLoad(savData)
	-- Patch the sav
	--liblsdj.parseSav(savData)
end

function Lsdj:onRomLoad(romData)

end

local function overclockPatch(data, overclock)
	local overClockIdx = data:findSequence({ 0x3e }, 1, { 0xe0, 0x07 })
	if overClockIdx ~= -1 then
		local v = 0x04
		if overclock == true then v = 0x07 end
		data:write(overClockIdx + 1, { v })
	end
end

function Lsdj:updateRom()
	local d = self:system().sourceRomData
	overclockPatch(d, self._overclock)
end

function Lsdj:onMenu(menu)
	local root = menu:subMenu("LSDj")
	local system = self:system()
	local rom = lsdj.loadRom(system.rom)
	local sav = lsdj.loadSav(system.sram)

	self:createSongsMenu(root:subMenu("Songs"), sav)
	self:createKitsMenu(root:subMenu("Kits"), rom)

	root:subMenu("Fonts")
			:parent()
		:subMenu("Palettes")
			:parent()
		:separator()
		:subMenu("Patches")
			:select("LitteFM", self._littleFm, function(v) self._littleFm = v; self:updateRom() end)
			:select("4x Overclock", self._overclock, function(v) self._overclock = v; self:updateRom() end)
			:parent()
		:action("Export ROM...", function() end)
end

function Lsdj:createKitsMenu(menu, rom)
	local system = self:system()

	menu:action("Import (and reset)...", function()
		dialog.loadFile({
			{ "Supported Files", { ".kit", ".gb" } },
			{ "LSDj Kit Files", ".kit" },
			{ "LSDj Rom Files", ".gb" },
		}, function(paths)
			if rom ~= nil then
				local err = rom:importKits(paths)
				if err == nil then
					system:setRom(rom:toBuffer(), true)
				else
					-- Log error
				end
			end
		end)
	end)
	:action("Export All...", function()
		dialog.saveDirectory(function(path)
			local rom = lsdj.loadRom(system.rom)
			if rom ~= nil then
				rom:exportKits(path)
			end
		end)
	end)
	:separator()
end

function Lsdj:createSongsMenu(menu, sav)
	local system = self:system()
	local desc = system:desc()

	menu:action("Import (and reset)...", function()
		dialog.loadFile({
			{ "Supported Files", { ".lsdsng", ".sav" } },
			{ "LSDj Song Files", ".lsdsng" },
			{ "LSDj Sav Files", ".sav" },
		}, function(paths)
			local err = sav:importSongs(paths)
			if err == nil then
				sav:toBuffer(system.sram)
				system:setSram(system.sram, true)
			else
				print("Import failed:")
				for _, v in ipairs(err) do
					print(v)
				end
			end
		end)
	end)
	:action("Export All...", function()
		dialog.saveDirectory(function(path)
			sav:exportSongs(path)
		end)
	end)
	:separator()

	for i, v in ipairs(names) do
		local songIdx = i - 2
		menu:subMenu(v.name)
			:action("Load (and reset)", function()
				sav:loadSong(songIdx)
				system:setSram(sav:toBuffer(), true)
			end)
			:action("Export .lsdsng...", function()
				dialog.saveFile({{ "LSDj Song Files", ".lsdsng" }}, function(path)
					sav:songToFile(songIdx, path)
				end)
			end)
			:action("Delete", function()
				deleteLsdjSong(desc.sourceSavData, songIdx)
				system:setSram(desc.sourceSavData, true)
			end)
	end
end

return Lsdj
