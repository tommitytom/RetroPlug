MidiNote = {
	C2 = 0,
	CSharp2 = 1
}

globalKeyMap({
	[Key.Tab] = Action.RetroPlug.NextInstance
})

keyMap({
	[Key.S] = Button.A,
	[Key.A] = Button.B,
	[Key.RightArrow] = Button.Right,
	[Key.LeftArrow] = Button.Left,
	[Key.UpArrow] = Button.Up,
	[Key.DownArrow] = Button.Down,
	[Key.Ctrl] = Button.Select,
	[Key.Enter] = Button.Start
})

keyMap({ system = "gameboy", romName = "LSDj*" }, {
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

padMap({ system = "gameboy", romName = "LSDj*" }, {
	[Pad.L1] = Action.Lsdj.UpTenRows,
	[Pad.R1] = Action.Lsdj.DownTenRows,
	[Pad.R2] = Button.Select
})

midiMap({ system = "gameboy", romName = "LSDj*" }, {
	[MidiNote.C2] = Button.A,
	[MidiNote.CSharp2] = Button.B
})

--[[_onKey({ vk = Key.Ctrl }, true)
_onKey({ vk = Key.C }, true)
_onKey({ vk = Key.Ctrl }, false)
_onKey({ vk = Key.C }, false)

_onPad(Pad.L1, true)
_onPad(Pad.L1, false)]]
