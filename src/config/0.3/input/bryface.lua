InputConfig({ name = "bryface" })

KeyMap({
	[Key.E] = Button.Up,
	[Key.S] = Button.Left,
	[Key.D] = Button.Down,
	[Key.F] = Button.Right,

	[Key.B] = Button.Select,
	[Key.N] = Button.Start,

	[Key.Num9] = Button.B,
	[Key.Num0] = Button.A
})

GlobalKeyMap({
	[Key.Tab] = Action.RetroPlug.NextInstance
})
