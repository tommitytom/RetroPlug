return {
	version = "0.0.1",
	name = "Example",
	author = "tommitytom",
	url = "https://tommitytom.co.uk",
	email = "fw@tommitytom.co.uk",
	copyright = "Tom Yaxley",

	audio = {
		inputs = 0,
		outputs = 8,
		midiIn = true,
		midiOut = false,
		latency = 0,
		stateChunks = true,
	},

	graphics = {
		width = 320,
		height = 288,
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
