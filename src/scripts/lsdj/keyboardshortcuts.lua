actions("lsdj.shortcuts", {
	DownTenRows = function()
		system.buttons:hold(Buttons.B):hold(Buttons.Down):releaseAll()
	end,
	UpTenRows = function()
		system.buttons:hold(Buttons.B):hold(Buttons.Up):releaseAll()
	end,
	ScreenUp = function() 
		system.buttons:hold(Buttons.Select):hold(Buttons.Up):releaseAll()
	end,
	ScreenDown = function() 
		system.buttons:hold(Buttons.Select):hold(Buttons.Down):releaseAll()
	end,
	ScreenLeft = function() 
		system.buttons:hold(Buttons.Select):hold(Buttons.Left):releaseAll()
	end,
	ScreenRight = function() 
		system.buttons:hold(Buttons.Select):hold(Buttons.Right):releaseAll()
	end,
	Delete = function()
		system.buttons:hold(Buttons.B):hold(Buttons.A):releaseAll()
	end,
	BeginSelection = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.B):releaseAll()
	end,
	CancelSelection = function() 
		system.buttons:press(Buttons.B) 
	end,
	Copy = function()
		system.buttons:press(Buttons.B)
	end,
	Cut = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.A):releaseAll()
	end,
	Paste = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.A):releaseAll()
	end,
})

{
	"lsdj.shortcuts" = {
		"Copy" = { "Ctrl", "C" } 
	}
}

ButtonStream = class()
function ButtonStream:init()
	self.state = { false, false, false, false, false, false, false, false }
end