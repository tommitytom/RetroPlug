local Lsdj = component({ name = "LSDj", romName = "LSDj*" })

local SelectState = {
	None = 0,
	RequestBegin = 1,
	Selecting = 2,
	Selected = 3
}

local function beginSelect(buttons)
	return buttons:hold(Button.Select):hold(Button.B):releaseAll()
end

local function endSelect(buttons)
	return buttons:press(Button.B)
end

local function isDirectionButton(button)
	return button == Button.Left or button == Button.Right or button == Button.Up or button == Button.Down
end

function Lsdj:init()
	self._valid = false
	self._kits = {}
	self._songs = {}
	self._littleFm = false
	self._overclock = false

	self._selectState = SelectState.None

	local buttons = self:system():buttons()
	self:registerActions({
		DownTenRows = function(down)
			if down == true then buttons:releaseAll():hold(Button.B):hold(Button.Down):releaseAll() end
		end,
		UpTenRows = function(down)
			if down == true then buttons:releaseAll():hold(Button.B):hold(Button.Up):releaseAll() end
		end,
		ScreenUp = function(down)
			if down == true then buttons:releaseAll():hold(Button.Select):hold(Button.Up):releaseAll() end
		end,
		ScreenDown = function(down)
			if down == true then buttons:releaseAll():hold(Button.Select):hold(Button.Down):releaseAll() end
		end,
		ScreenLeft = function(down)
			if down == true then buttons:releaseAll():hold(Button.Select):hold(Button.Left):releaseAll() end
		end,
		ScreenRight = function(down)
			if down == true then buttons:releaseAll():hold(Button.Select):hold(Button.Right):releaseAll() end
		end,
		Delete = function(down)
			if down == true then
				if self._selectState == SelectState.None then
					buttons:releaseAll():hold(Button.B):hold(Button.A):releaseAll()
				elseif self._selectState == SelectState.Selecting or self._selectState == SelectState.Selected then
					buttons:hold(Button.Select):hold(Button.A):releaseAll()
					self._selectState = SelectState.None
				end
			end
		end,
		BeginSelection = function(down)
			if down == true then
				if self._selectState == SelectState.None then
					self._selectState = SelectState.RequestBegin
					buttons:hold(Button.Select)
				elseif self._selectState == SelectState.Selected then
					self._selectState = SelectState.Selecting
				end
			else
				if self._selectState == SelectState.Selecting then
					self._selectState = SelectState.Selected
				elseif self._selectState == SelectState.RequestBegin then
					self._selectState = SelectState.None
					buttons:release(Button.Select)
				end
			end
		end,
		CancelSelection = function(down)
			if down == true then
				if self._selectState == SelectState.Selecting or self._selectState == SelectState.Selected then
					endSelect(buttons)
					self._selectState = SelectState.None
				end
			end
		end,
		Copy = function(down)
			if down == true then
				if self._selectState == SelectState.None or self._selectState == SelectState.RequestBegin then
					beginSelect(buttons)
				end

				buttons:releaseAll():press(Button.B)
				self._selectState = SelectState.None
			end
		end,
		Cut = function(down)
			if down == true then
				if self._selectState == SelectState.None or self._selectState == SelectState.RequestBegin then
					beginSelect(buttons)
				end

				buttons:releaseAll():hold(Button.Select):hold(Button.A):releaseAll()
				self._selectState = SelectState.None
			end
		end,
		Paste = function(down)
			if down == true then buttons:hold(Button.Select):hold(Button.A):releaseAll() end
		end,
	})
end

function Lsdj:onBeforeButton(button, down)
	if self._selectState == SelectState.RequestBegin then
		if down == true then
			if isDirectionButton(button) == true then
				beginSelect(self:buttons()):hold(button)
				self._selectState = SelectState.Selecting
				return false
			end
		end
	elseif self._selectState == SelectState.Selected then
		if down == true then
			if isDirectionButton(button) == true then
				endSelect(self:buttons()):hold(button)
				self._selectState = SelectState.None
				return false
			else
			end
		end
	end
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

function Lsdj:onRomLoad(system)

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

	self:createSongsMenu(root:subMenu("Songs"))

	root:subMenu("Kits")
			:parent()
		:subMenu("Fonts")
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

function Lsdj:createSongsMenu(menu)
	local system = self:system()
	local desc = system:desc()

	menu:action("Import (and reset)...", function() end)
		:action("Export All...", function() end)
		:separator()

	local names = getLsdjSongNames(desc.sourceSavData)

	for i, v in ipairs(names) do
		menu:subMenu(v.name)
				:action("Load (and reset)", function()
					loadLsdjSong(desc.sourceSavData, i - 2)
					system:setSram(desc.sourceSavData, true)
				end)
				:action("Export .lsdsng...", function()
					--[[dialog.saveFile({ ".lsdsng" }, function(path) {
						local songData = saveLsdjSong(system.sourceSavData, i - 2)
						fs.saveFile(songData, path)
					end)]]
				end)
				:action("Delete", function()
					deleteLsdjSong(desc.sourceSavData, i - 2)
					system:setSram(desc.sourceSavData, true)
				end)
	end
end

return Lsdj
