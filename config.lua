return {
	plugin = {
		version = "0.3.2",
		name = "RetroPlug",
		author = "tommitytom",
		uniqueId = "2wvF",
		authorId = "tmtt",
		url = "https://tommitytom.co.uk",
		email = "retroplug@tommitytom.co.uk",
		copyright = "Copyright 2021 Tom Yaxley"
	},
	config = {
		default = {
			type = "synth",
			latency = 0,
			midiIn = true,
			midiOut = false,
			stateChunks = true,
			sharedResources = false,
			inputs = 0,
			outputs = 8,

			-- Add these to graphics?
			ui = true,
			width = 320,
			height = 288,
			fps = 60,

			graphics = {
				platform = "gl2",
				backend = "nanovg",
				vsync = true
			}
		},
		vst2 = {
			outDir32 = "C:/vst32",
			outDir64 = "C:/vst64"
		},
		app = {
			outputs = 2,

			-- App specific
			debugConsole = true,
			allowMultiple = true
		}
	}
}
