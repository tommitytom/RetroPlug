--[[InputConfig({
	name = "Default",
	author = "tommitytom"
})]]

KeyMap({
	[Key.S] = Button.A,
	[Key.A] = Button.B,
	[Key.RightArrow] = Button.Right,
	[Key.LeftArrow] = Button.Left,
	[Key.UpArrow] = Button.Up,
	[Key.DownArrow] = Button.Down,
	[Key.Ctrl] = Button.Select,
	[Key.Enter] = Button.Start
})

GlobalKeyMap({
	[Key.Tab] = Action.RetroPlug.NextInstance
})

-- Keys and shortcuts specific to LSDj
KeyMap({ romName = "LSDj*" }, {
	[{ Key.Ctrl, Key.X }] = Action.Lsdj.Cut,
	[{ Key.Ctrl, Key.C }] = Action.Lsdj.Copy,
	[{ Key.Ctrl, Key.V }] = Action.Lsdj.Paste,
	[Key.Delete] = Action.Lsdj.Delete,
	[Key.PageDown] = Action.Lsdj.DownTenRows,
	[Key.PageUp] = Action.Lsdj.UpTenRows,
	[Key.Shift] = Action.Lsdj.BeginSelection,
	[Key.Esc] = Action.Lsdj.CancelSelection,
})

PadMap({
	[Pad.A] = Button.B,
	[Pad.B] = Button.A,
	[Pad.LeftStickLeft] = Button.Left,
	[Pad.LeftStickRight] = Button.Right,
	[Pad.LeftStickUp] = Button.Up,
	[Pad.LeftStickDown] = Button.Down,
	[Pad.Left] = Button.Left,
	[Pad.Right] = Button.Right,
	[Pad.Up] = Button.Up,
	[Pad.Down] = Button.Down,
	[Pad.Select] = Button.Select,
	[Pad.Start] = Button.Start
})

GlobalPadMap({
	[Pad.Y] = Action.RetroPlug.NextInstance
})

-- Gamepad button presses specific to LSDj
PadMap({ romName = "LSDj*" }, {
	[Pad.L1] = Action.Lsdj.UpTenRows,
	[Pad.R1] = Action.Lsdj.DownTenRows,
	[Pad.R2] = Button.Select
})
