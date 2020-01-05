local Lsdj = component({ name = "Lsdj", romName = "LSDj*" })

local inspect = require("inspect")

function Lsdj:init()
	self._valid = false
	self._kits = {}
	self._songs = {}

	self._selecting = false

	local buttons = self.buttons
	self:registerActions({
		DownTenRows = function(down)
			if down == true then buttons:hold(Button.B):hold(Button.Down):releaseAll() end
		end,
		UpTenRows = function(down)
			if down == true then buttons:hold(Button.B):hold(Button.Up):releaseAll() end
		end,
		ScreenUp = function(down)
			if down == true then buttons:hold(Button.Select):hold(Button.Up):releaseAll() end
		end,
		ScreenDown = function(down)
			if down == true then buttons:hold(Button.Select):hold(Button.Down):releaseAll() end
		end,
		ScreenLeft = function(down)
			if down == true then buttons:hold(Button.Select):hold(Button.Left):releaseAll() end
		end,
		ScreenRight = function(down)
			if down == true then buttons:hold(Button.Select):hold(Button.Right):releaseAll() end
		end,
		Delete = function(down)
			if down == true then buttons:hold(Button.B):hold(Button.A):releaseAll() end
		end,
		BeginSelection = function(down)
			print("begin")
			print(self._selecting)
			if down == true then buttons:hold(Button.Select):hold(Button.B):releaseAll() end
		end,
		CancelSelection = function(down)
			if down == true then buttons:hold(Button.B):releaseAll() end
		end,
		Copy = function(down)
			if down == true then buttons:hold(Button.B):releaseAll() end
		end,
		Cut = function(down)
			if down == true then buttons:hold(Button.Select):hold(Button.A):releaseAll() end
		end,
		Paste = function(down)
			if down == true then buttons:hold(Button.Select):hold(Button.A):releaseAll() end
		end,
	})
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

return Lsdj
