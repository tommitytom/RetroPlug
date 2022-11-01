{
	-- Default system settings
	system = {
		uiComponents = {},
		audioComponents = {},
		sameBoy = {
			model = "auto", -- auto, dmgB, cgbE, cgbC, agb
			gameLink = false,
			skipBootRom = false
		},
		input = {
			key = "default.lua",
			pad = "default.lua"
		}
	},
	-- Default project settings
	project = {
		saveType = "sram", --sram, state
		audioRouting = "stereoMixDown",
		zoom = 2,
		midiRouting = "sendToAll",
		layout = "auto",
		includeRom = true
	}
}
