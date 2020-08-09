InputConfig({
	name = "Default",
	author = "tommitytom"
})

KeyMap({
	[Key.UpArrow] = Button.Up,
	[Key.LeftArrow] = Button.Left,
	[Key.DownArrow] = Button.Down,
	[Key.RightArrow] = Button.Right,

	[Key.Ctrl] = Button.Select,
	[Key.Enter] = Button.Start,

	[Key.W] = Button.B,
	[Key.D] = Button.A,
})

GlobalKeyMap({
	[Key.Tab] = Action.RetroPlug.NextSystem,
	[{ Key.Ctrl, Key.S }] = Action.RetroPlug.SaveProject
})

PadMap({
	[Pad.Up] = Button.Up,
	[Pad.Left] = Button.Left,
	[Pad.Down] = Button.Down,
	[Pad.Right] = Button.Right,
	[Pad.LeftStickUp] = Button.Up,
	[Pad.LeftStickLeft] = Button.Left,
	[Pad.LeftStickDown] = Button.Down,
	[Pad.LeftStickRight] = Button.Right,

	[Pad.Select] = Button.Select,
	[Pad.Start] = Button.Start,

	[Pad.A] = Button.B,
	[Pad.B] = Button.A
})

GlobalPadMap({
	[Pad.Y] = Action.RetroPlug.NextSystem
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

-- Gamepad button presses specific to LSDj
PadMap({ romName = "LSDj*" }, {
	[Pad.X] = Action.Lsdj.BeginSelection,
	[Pad.L1] = Action.Lsdj.UpTenRows,
	[Pad.R1] = Action.Lsdj.DownTenRows,
	[Pad.R2] = Button.Select,
	[Pad.RightStickUp] = Action.Lsdj.ScreenUp,
	[Pad.RightStickLeft] = Action.Lsdj.ScreenLeft,
	[Pad.RightStickDown] = Action.Lsdj.ScreenDown,
	[Pad.RightStickRight] = Action.Lsdj.ScreenRight
})
