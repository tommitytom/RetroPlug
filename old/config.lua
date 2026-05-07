return {
	version = "0.4.0",
	name = "RetroPlug",
	author = "tommitytom",
	url = "https://retroplug.io",
	email = "hello@retroplug.io",
	copyright = "Tom Yaxley",

	audio = {
		inputs = 0,
		outputs = 2,
		midiIn = true,
		midiOut = false,
		latency = 0,
		stateChunks = true,
	},

	graphics = {
		width = 640,
		height = 576,
		fps = 60,
		vsync = true
	},

	plugin = {
		uniqueId = "2wvF",
		authorId = "tmtt",
		type = "synth",
		sharedResources = false,
	}
}
