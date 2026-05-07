return {
	settings = {
		outDir = "retroplug/luawrapper/generated",
	},
	modules = {
		audio = { path = "retroplug/scripts/audio" },
		common = { path = "retroplug/scripts/common" },
		ui = { path = "retroplug/scripts/ui" },
		config = { path = "config/0.3", compile = false }
	}
}
