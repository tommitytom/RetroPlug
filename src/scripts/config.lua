Action = {
	Lsdj = {
		Copy = function() print("Lsdj.Copy") end,
		Paste = function() print("Lsdj.Paste") end,
		DownTenRows = function() print("Lsdj.DownTenRows") end,
		UpTenRows = function() print("Lsdj.UpTenRows") end,
	},
	RetroPlug = {
		NextInstance = function() print("RetroPlug.NextInstance") end
	}
}

MidiNote = {
	C2 = 0,
	CSharp2 = 1
}

keyMap({
	[Key.S] = Button.A,
	[Key.A] = Button.B,
	[Key.RightArrow] = Button.Right,
	[Key.LeftArrow] = Button.Left,
	[Key.UpArrow] = Button.Up,
	[Key.DownArrow] = Button.Down,
	[Key.Ctrl] = Button.Select,
	[Key.Enter] = Button.Start,
	[Key.Tab] = Action.RetroPlug.NextInstance
})

keyMap({ system = "gameboy", romName = "lsdj*" }, {
	[{ Key.Ctrl, Key.C }] = Action.Lsdj.Copy,
	[{ Key.Ctrl, Key.V }] = Action.Lsdj.Paste,
	[Key.PageDown] = Action.Lsdj.DownTenRows,
	[Key.PageUp] = Action.Lsdj.UpTenRows,
})

padMap({ system = "gameboy" }, {
	[Pad.A] = Button.B,
	[Pad.B] = Button.A,
	[Pad.Right] = Button.Right,
	[Pad.Left] = Button.Left,
	[Pad.Up] = Button.Up,
	[Pad.Down] = Button.Down,
	[Pad.Select] = Button.Select,
	[Pad.Start] = Button.Start
})

padMap({ system = "gameboy", romName = "lsdj*" }, {
	[Pad.L1] = Action.Lsdj.UpTenRows,
	[Pad.R1] = Action.Lsdj.DownTenRows,
	[Pad.R2] = Button.Select
})

midiMap({ system = "gameboy", romName = "lsdj*" }, {
	[MidiNote.C2] = Button.A,
	[MidiNote.CSharp2] = Button.B
})

_setup("gameboy", "lsdj")

_onKey({ vk = Key.Ctrl }, true)
_onKey({ vk = Key.C }, true)
_onKey({ vk = Key.Ctrl }, false)
_onKey({ vk = Key.C }, false)

_onPad(Pad.L1, true)
_onPad(Pad.L1, false)