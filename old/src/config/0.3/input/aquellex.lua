InputConfig({ name = "Aquellex" })

KeyMap({
	[Key.W] = Button.Up,
	[Key.A] = Button.Left,
	[Key.S] = Button.Down,
	[Key.D] = Button.Right,

	[Key.C] = Button.Select,
	[Key.V] = Button.Start,

	[Key.G] = Button.B,
	[Key.H] = Button.A
})

GlobalKeyMap({
	[Key.Tab] = Action.RetroPlug.NextInstance
})
