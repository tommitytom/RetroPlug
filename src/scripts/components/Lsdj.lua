local Lsdj = component({ name = "Lsdj", romName = "LSDj*" })

function Lsdj:init()
	self._valid = false
	self._kits = {}
	self._songs = {}
end

function Lsdj:onBeforeRomLoad(romData)
	-- Patch the rom
	--liblsdj.parseRom(romData)
end

function Lsdj:onBeforeSavLoad(savData)
	-- Patch the sav
	--liblsdj.parseSav(savData)
end

function Lsdj:onRomLoaded(system)
	--[[self.registerActions({
		DownTenRows = function()
			system.buttons:hold(Button.B):delay():hold(Button.Down):releaseAll()
		end,
		UpTenRows = function()
			system.buttons:hold(Button.B):hold(Button.Up):releaseAll()
		end,
		ScreenUp = function()
			system.buttons:hold(Button.Select):hold(Button.Up):releaseAll()
		end,
		ScreenDown = function()
			system.buttons:hold(Button.Select):hold(Button.Down):releaseAll()
		end,
		ScreenLeft = function()
			system.buttons:hold(Button.Select):hold(Button.Left):releaseAll()
		end,
		ScreenRight = function()
			system.buttons:hold(Button.Select):hold(Button.Right):releaseAll()
		end,
		Delete = function()
			system.buttons:hold(Button.B):hold(Button.A):releaseAll()
		end,
		BeginSelection = function()
			system.buttons:hold(Button.Select):hold(Button.B):releaseAll()
		end,
		CancelSelection = function()
			system.buttons:press(Button.B)
		end,
		Copy = function()
			system.buttons:press(Button.B)
		end,
		Cut = function()
			system.buttons:hold(Button.Select):hold(Button.A):releaseAll()
		end,
		Paste = function()
			system.buttons:hold(Button.Select):hold(Button.A):releaseAll()
		end,
	})]]
end

return Lsdj
