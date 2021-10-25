InputConfig({ name = "bryface" })

KeyMap({
	[Key.3] = Button.Up,
	[Key.W] = Button.Left,
	[Key.E] = Button.Down,
	[Key.R] = Button.Right,

	[Key.B] = Button.Select,
	[Key.N] = Button.Start,

	[Key.Num9] = Button.B,
	[Key.Num0] = Button.A
})

GlobalKeyMap({
	[Key.Tab] = Action.RetroPlug.NextInstance
})
